//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.media.MediaCodec
import android.telephony.Rlog
import java.util.concurrent.ArrayBlockingQueue

object SipDownlinkAudioDecoder {
    fun queueCodecFrameAndDrainPcm(
        logTag: String,
        decoder: SipMediaCodec,
        codecFrame: ByteArray,
        pcmQueue: ArrayBlockingQueue<ByteArray>,
    ) {
        val inBufIndex = decoder.dequeueInputBuffer(-1)
        val inBuf = decoder.getInputBuffer(inBufIndex)!!
        inBuf.clear()
        inBuf.put(codecFrame)
        decoder.queueInputBuffer(inBufIndex, 0, codecFrame.size, 0, 0)

        // Drain decoder output. Some AMR modes do not produce an output buffer
        // immediately with a zero-timeout dequeue on all codecs, so give it a tiny
        // real-time budget for the first buffer and then drain anything else.
        val outBufInfo = MediaCodec.BufferInfo()
        var drainTimeoutUs = 10_000L
        while (true) {
            val outBufIndex = decoder.dequeueOutputBuffer(outBufInfo, drainTimeoutUs)
            drainTimeoutUs = 0L
            if (outBufIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                Rlog.d(logTag, "Decoder output format changed")
                continue
            }
            if (outBufIndex < 0) break

            val outBuf = decoder.getOutputBuffer(outBufIndex)!!
            val pcm = ByteArray(outBufInfo.size)
            outBuf.position(outBufInfo.offset)
            outBuf.limit(outBufInfo.offset + outBufInfo.size)
            outBuf.get(pcm)
            if (!pcmQueue.offer(pcm)) {
                pcmQueue.poll()
                if (!pcmQueue.offer(pcm)) {
                    Rlog.w(logTag, "Downlink PCM queue still full after dropping oldest frame")
                }
            }
            decoder.releaseOutputBuffer(outBufIndex, false)
        }
    }
}
