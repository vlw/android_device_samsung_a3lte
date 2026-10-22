//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.media.AudioRecord
import android.media.MediaCodec
import android.telephony.Rlog

data class SipUplinkAudioCapture(
    val audioRecord: AudioRecord,
    val bufferSize: Int,
    val previousAudioMode: Int,
)

object SipUplinkAudioCaptureStarter {
    fun start(
        logTag: String,
        context: Context,
        audioCodec: NegotiatedAudioCodec,
        encoder: SipMediaCodec,
    ): SipUplinkAudioCapture? {
        // DANGER: Don't open the mic before the user acknowledged opening the call!

        val minBufferSize = SipAudioRecordFactory.minBufferSize(audioCodec)
        if (minBufferSize <= 0) {
            Rlog.e(logTag, "AudioRecord.getMinBufferSize failed: $minBufferSize")
            try {
                encoder.stop()
            } catch (_: Throwable) {
            }
            try {
                encoder.release()
            } catch (_: Throwable) {
            }
            return null
        }

        val audioRecord = try {
            // The loop reads single frames, so extra capacity is only overrun
            // headroom for a late wakeup; it does not add capture latency.
            SipAudioRecordFactory.createVoiceCommunicationRecord(
                bufferSize = minBufferSize * 2,
                audioCodec = audioCodec,
            )
        } catch (t: Throwable) {
            Rlog.e(logTag, "AudioRecord creation failed with bufferSize=${minBufferSize * 2}", t)
            try {
                encoder.stop()
            } catch (_: Throwable) {
            }
            try {
                encoder.release()
            } catch (_: Throwable) {
            }
            return null
        }
        Rlog.d(logTag, "AudioRecord created with minBufferSize=$minBufferSize, state=${audioRecord.state}")

        val audioManager = SipAudioRecordRouting.pinBuiltinMic(
            logTag = logTag,
            context = context,
            audioRecord = audioRecord,
        )

        val previousAudioMode = SipAudioRecordStarter.startForImsUplink(
            logTag = logTag,
            audioManager = audioManager,
            audioRecord = audioRecord,
            encoder = encoder,
        ) ?: return null

        return SipUplinkAudioCapture(
            audioRecord = audioRecord,
            bufferSize = minBufferSize,
            previousAudioMode = previousAudioMode,
        )
    }
}
