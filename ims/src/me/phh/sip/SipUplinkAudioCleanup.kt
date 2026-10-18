//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.media.AudioRecord
import android.media.MediaCodec
import android.telephony.Rlog
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

object SipUplinkAudioCleanup {
    fun cleanup(
        logTag: String,
        context: Context,
        audioRecord: AudioRecord,
        encoder: SipMediaCodec,
        callStopped: AtomicBoolean,
        callGeneration: AtomicInteger,
        generation: Int,
        totalPacketsSent: Int,
        previousAudioMode: Int,
    ) {
        Rlog.d(
            logTag,
            "Encode thread exiting: callStopped=${callStopped.get()}, " +
                "genMismatch=${callGeneration.get() != generation}, " +
                "totalPacketsSent=$totalPacketsSent"
        )
        try {
            audioRecord.stop()
        } catch (t: Throwable) {
            Rlog.d(logTag, "AudioRecord stop failed during encode cleanup", t)
        }
        try {
            audioRecord.release()
        } catch (t: Throwable) {
            Rlog.d(logTag, "AudioRecord release failed during encode cleanup", t)
        }
        try {
            encoder.stop()
        } catch (t: Throwable) {
            Rlog.d(logTag, "Encoder stop failed during encode cleanup", t)
        }
        try {
            encoder.release()
        } catch (t: Throwable) {
            Rlog.d(logTag, "Encoder release failed during encode cleanup", t)
        }
        val stopped = callStopped.get()
        val genMismatch = callGeneration.get() != generation
        Rlog.d(
            logTag,
            "Encode thread cleanup complete before audio mode restore: " +
                "callStopped=$stopped genMismatch=$genMismatch"
        )
        if (genMismatch) {
            Rlog.i(
                logTag,
                "Skipping audio mode restore from stale encode media generation: " +
                    "callStopped=$stopped genMismatch=$genMismatch totalPacketsSent=$totalPacketsSent",
            )
        } else {
            SipAudioModeRestorer.restoreAfterImsCall(
                logTag = logTag,
                context = context,
                reason = "encode thread cleanup",
                previousMode = previousAudioMode,
            )
        }
    }
}
