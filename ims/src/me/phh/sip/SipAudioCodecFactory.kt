//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.media.MediaCodec
import android.media.MediaFormat

object SipAudioCodecFactory {
    fun createStartedDecoder(
        audioCodec: NegotiatedAudioCodec,
    ): SipMediaCodec {
        if (audioCodec.isEvs) return SipMediaCodec.evs(audioCodec,false)
        val decoder = MediaCodec.createDecoderByType(audioCodec.mimeType)
        val mediaFormat = MediaFormat.createAudioFormat(
            audioCodec.mimeType,
            audioCodec.sampleRate,
            audioCodec.channelCount,
        )
        decoder.configure(mediaFormat, null, null, 0)
        decoder.start()
        return SipMediaCodec.android(decoder)
    }

    fun createStartedEncoder(
        audioCodec: NegotiatedAudioCodec,
    ): SipMediaCodec {
        if (audioCodec.isEvs) return SipMediaCodec.evs(audioCodec,true)
        val encoder = MediaCodec.createEncoderByType(audioCodec.mimeType)
        val mediaFormat = MediaFormat.createAudioFormat(
            audioCodec.mimeType,
            audioCodec.sampleRate,
            audioCodec.channelCount,
        )
        mediaFormat.setInteger(MediaFormat.KEY_BIT_RATE, audioCodec.bitRate)
        mediaFormat.setInteger(MediaFormat.KEY_PRIORITY, 0) //  0 = realtime priority, encoder will not fall behind
        encoder.configure(mediaFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        encoder.start()
        return SipMediaCodec.android(encoder)
    }
}
