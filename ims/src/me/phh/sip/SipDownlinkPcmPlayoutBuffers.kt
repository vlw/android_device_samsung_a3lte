//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

data class SipDownlinkPcmPlayoutBuffers(
    val frameBytes: Int,
    val silenceFrame: ByteArray,
    val pcmQueue: ArrayBlockingQueue<ByteArray>,
    val running: AtomicBoolean,
) {
    companion object {
        fun create(
            audioCodec: NegotiatedAudioCodec,
            queueCapacity: Int = 8,
            minimumFrameBytes: Int = 320,
        ): SipDownlinkPcmPlayoutBuffers {
            val frameBytes = ((audioCodec.sampleRate / 50) * audioCodec.channelCount * 2)
                .coerceAtLeast(minimumFrameBytes)

            return SipDownlinkPcmPlayoutBuffers(
                frameBytes = frameBytes,
                silenceFrame = ByteArray(frameBytes),
                pcmQueue = ArrayBlockingQueue(queueCapacity),
                running = AtomicBoolean(true),
            )
        }
    }
}
