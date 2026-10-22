//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.media.AudioTrack
import android.os.SystemClock
import android.telephony.Rlog
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

object SipDownlinkPcmPlayout {
    private const val DOWNLINK_UNDERRUN_CONCEALMENT_FRAMES = 6
    // Audio held in the AudioTrack ahead of the mixer. On the low-latency VoIP
    // output (4 ms HAL periods) 40 ms measured ~100 ms write-to-earpiece with no
    // underruns; 60 ms was sized for the 40 ms pulls of the deep-buffer output.
    private const val DOWNLINK_TARGET_BUFFERED_MS = 40L
    private const val DOWNLINK_MAX_QUEUED_FRAMES = 4 // 80 ms
    private const val DOWNLINK_POLL_MS = 5L
    private const val DOWNLINK_HEAD_STALL_MS = 200L

    private fun attenuatePcm16LeFrame(frame: ByteArray, gainShift: Int): ByteArray {
        val out = ByteArray(frame.size)
        var i = 0
        while (i + 1 < frame.size) {
            val sample =
                (frame[i].toInt() and 0xff) or
                    (frame[i + 1].toInt() shl 8)
            val scaled = sample.toShort().toInt() shr gainShift
            out[i] = (scaled and 0xff).toByte()
            out[i + 1] = ((scaled shr 8) and 0xff).toByte()
            i += 2
        }
        return out
    }

    fun start(
        logTag: String,
        audioTrack: AudioTrack,
        audioCodec: NegotiatedAudioCodec,
        buffers: SipDownlinkPcmPlayoutBuffers,
        callStopped: AtomicBoolean,
        callGeneration: AtomicInteger,
        generation: Int,
        codecConcealment: (() -> ByteArray?)? = null,
    ): Thread =
        thread(name = "PhhDownlinkPcmPlayout") {
            try {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_AUDIO)
                Rlog.d(logTag, "Downlink PCM playout thread priority set to urgent audio")
            } catch (t: Throwable) {
                Rlog.w(logTag, "Failed to set downlink PCM playout thread priority", t)
            }

            var fillerFrames = 0
            var lastGoodPcmFrame: ByteArray? = null
            // Pace by what AudioTrack actually still holds, not by wall clock.
            // Writing a filler frame every 20 ms while the track was still full
            // added 20 ms of permanent latency on every network jitter event.
            val sampleRate = audioCodec.sampleRate.toLong()
            var writtenFrames = 0L
            var lastHead = -1L
            var lastHeadChangeMs = SystemClock.elapsedRealtime()
            var trimmedFrames = 0
            try {
                audioTrack.setStartThresholdInFrames(buffers.frameBytes / 2)
            } catch (t: Throwable) {
                Rlog.w(logTag, "Cannot lower downlink AudioTrack start threshold", t)
            }
            Rlog.d(logTag, "Downlink PCM playout started: frameBytes=${buffers.frameBytes} codec=${audioCodec.name}/${audioCodec.sampleRate} gen=$generation")
            try {
                while (buffers.running.get() && !callStopped.get() && callGeneration.get() == generation) {
                    val now = SystemClock.elapsedRealtime()
                    val head = audioTrack.playbackHeadPosition.toLong() and 0xffffffffL
                    if (head != lastHead) {
                        lastHead = head
                        lastHeadChangeMs = now
                    }
                    val bufferedMs = (writtenFrames - head).coerceAtLeast(0L) * 1000L / sampleRate
                    // Bound queued latency: drop the oldest decoded audio.
                    while (buffers.pcmQueue.size > DOWNLINK_MAX_QUEUED_FRAMES) {
                        buffers.pcmQueue.poll()
                        trimmedFrames++
                        if (trimmedFrames == 1 || trimmedFrames % 50 == 0) {
                            Rlog.d(logTag, "Downlink PCM playout trimmed latency frames=$trimmedFrames gen=$generation")
                        }
                    }
                    // A stalled head (track not started yet) must not block writes.
                    val headStalled = now - lastHeadChangeMs > DOWNLINK_HEAD_STALL_MS
                    if (bufferedMs >= DOWNLINK_TARGET_BUFFERED_MS && !headStalled) {
                        Thread.sleep(DOWNLINK_POLL_MS)
                        continue
                    }

                    val queuedPcm = buffers.pcmQueue.poll()
                    val pcm =
                        if (queuedPcm != null) {
                            lastGoodPcmFrame = queuedPcm
                            queuedPcm
                        } else {
                            val codecFrame = codecConcealment?.invoke()
                            val last = lastGoodPcmFrame
                            if (codecFrame != null) codecFrame
                            else if (last != null && fillerFrames < DOWNLINK_UNDERRUN_CONCEALMENT_FRAMES) {
                                attenuatePcm16LeFrame(
                                    last,
                                    gainShift = (fillerFrames + 2).coerceAtMost(8),
                                )
                            } else {
                                buffers.silenceFrame
                            }
                        }

                    if (queuedPcm == null) {
                        fillerFrames++
                        if (pcm !== buffers.silenceFrame) {
                            if (fillerFrames == 1 || fillerFrames == DOWNLINK_UNDERRUN_CONCEALMENT_FRAMES) {
                                Rlog.d(
                                    logTag,
                                    "Downlink PCM playout concealed underrun frames=$fillerFrames " +
                                        "queued=${buffers.pcmQueue.size} bufferedMs=$bufferedMs gen=$generation",
                                )
                            }
                        } else if (fillerFrames == 1 || fillerFrames % 50 == 0) {
                            Rlog.d(logTag, "Downlink PCM playout filler frames=$fillerFrames queued=${buffers.pcmQueue.size} gen=$generation")
                        }
                    } else if (fillerFrames > 0) {
                        Rlog.d(logTag, "Downlink PCM playout recovered after fillerFrames=$fillerFrames queued=${buffers.pcmQueue.size} bufferedMs=$bufferedMs gen=$generation")
                        fillerFrames = 0
                    }

                    val written = audioTrack.write(pcm, 0, pcm.size, AudioTrack.WRITE_BLOCKING)
                    if (written > 0) writtenFrames += written / 2
                    if (headStalled) lastHeadChangeMs = SystemClock.elapsedRealtime()
                }
            } catch (_: InterruptedException) {
                // Normal during call teardown.
            } catch (t: Throwable) {
                Rlog.w(logTag, "Downlink PCM playout failed", t)
            }
            Rlog.d(logTag, "Downlink PCM playout exiting: running=${buffers.running.get()} callStopped=${callStopped.get()} genMismatch=${callGeneration.get() != generation} queued=${buffers.pcmQueue.size}")
        }
}
