//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import java.util.concurrent.atomic.AtomicInteger

object SipDtmfTimestampAllocator {
    fun allocate(
        audioCodec: NegotiatedAudioCodec,
        durationMs: Int,
        mediaTimestampSamples: AtomicInteger,
        dtmfTimestampSamples: AtomicInteger,
    ): Int {
        val safeDurationMs = durationMs.coerceAtLeast(160)

        // One telephone-event uses one fixed timestamp for all repeats, but the
        // next digit must not reuse that timestamp. Keep at least one event
        // duration plus 40ms between synthetic timestamps when media is stalled.
        val minimumStepSamples = ((safeDurationMs + 40) * audioCodec.rtpClockRate) / 1000

        while (true) {
            val mediaTimestamp = mediaTimestampSamples.get()
            val previousDtmfTimestamp = dtmfTimestampSamples.get()
            val candidate = if (previousDtmfTimestamp <= 0) {
                mediaTimestamp.coerceAtLeast(audioCodec.rtpTimestampStep)
            } else {
                maxOf(mediaTimestamp, previousDtmfTimestamp + minimumStepSamples)
            }

            if (dtmfTimestampSamples.compareAndSet(previousDtmfTimestamp, candidate)) {
                return candidate
            }
        }
    }
}
