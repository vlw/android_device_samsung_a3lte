//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.media.AudioTrack
import android.media.MediaCodec
import android.telephony.Rlog
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

object SipDownlinkAudioCleanup {
    fun cleanup(
        logTag: String,
        context: Context,
        audioTrack: AudioTrack,
        decoder: SipMediaCodec,
        playoutBuffers: SipDownlinkPcmPlayoutBuffers,
        playoutThread: Thread,
        callStopped: AtomicBoolean,
        callGeneration: AtomicInteger,
        generation: Int,
        receivedCount: Int,
        previousAudioMode: Int,
    ) {
        playoutBuffers.running.set(false)
        try {
            playoutThread.interrupt()
        } catch (t: Throwable) {
            Rlog.d(logTag, "Downlink playout interrupt failed during decode cleanup", t)
        }
        try {
            audioTrack.stop()
        } catch (t: Throwable) {
            Rlog.d(logTag, "AudioTrack stop failed during decode cleanup", t)
        }
        try {
            audioTrack.release()
        } catch (t: Throwable) {
            Rlog.d(logTag, "AudioTrack release failed during decode cleanup", t)
        }
        try {
            decoder.stop()
        } catch (t: Throwable) {
            Rlog.d(logTag, "Decoder stop failed during decode cleanup", t)
        }
        try {
            decoder.release()
        } catch (t: Throwable) {
            Rlog.d(logTag, "Decoder release failed during decode cleanup", t)
        }
        val stopped = callStopped.get()
        val genMismatch = callGeneration.get() != generation
        if (genMismatch) {
            Rlog.i(
                logTag,
                "Skipping audio mode restore from stale decode media generation: " +
                    "callStopped=$stopped genMismatch=$genMismatch received=$receivedCount",
            )
        } else {
            SipAudioModeRestorer.restoreAfterImsCall(
                logTag = logTag,
                context = context,
                reason = "decode thread cleanup",
                previousMode = previousAudioMode,
            )
        }
        Rlog.d(logTag, "Decode thread cleanup complete: callStopped=$stopped genMismatch=$genMismatch received=$receivedCount")
    }
}
