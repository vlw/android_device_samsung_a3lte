//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.media.MediaCodec
import android.telephony.Rlog

object SipAudioCodecNegotiator {
    private val widebandAvailable by lazy { isMediaCodecAvailableFor("PhhCodec",SipAudioCodecs.AMR_WB) }
    fun selectRenegotiation(attributes: List<String>, payloads: Set<Int>, logTag: String): NegotiatedAudioCodec {
        val sdp=listOf("m=audio 9 RTP/AVP ${payloads.joinToString(" ")}")+attributes.map { "a=$it" }
        return selectIncomingSpeechCodecFromOffer(logTag,sdp,"dialog renegotiation",widebandAvailable)
    }

    fun isMediaCodecAvailableFor(
        logTag: String,
        audioCodec: NegotiatedAudioCodec,
    ): Boolean {
        var encoder: MediaCodec? = null
        var decoder: MediaCodec? = null
        return try {
            encoder = MediaCodec.createEncoderByType(audioCodec.mimeType)
            decoder = MediaCodec.createDecoderByType(audioCodec.mimeType)
            Rlog.d(logTag, "MediaCodec available for ${audioCodec.name}: mime=${audioCodec.mimeType}")
            true
        } catch (t: Throwable) {
            Rlog.w(logTag, "MediaCodec unavailable for ${audioCodec.name}: mime=${audioCodec.mimeType}", t)
            false
        } finally {
            try { encoder?.release() } catch (_: Throwable) { }
            try { decoder?.release() } catch (_: Throwable) { }
        }
    }

    fun speechCodecRtpmapName(audioCodec: NegotiatedAudioCodec): String =
        "${audioCodec.sdpCodecName}/${audioCodec.rtpClockRate}"

    fun telephoneEventRtpmapName(audioCodec: NegotiatedAudioCodec): String =
        "telephone-event/${audioCodec.rtpClockRate}"

    fun defaultSpeechFmtpAnswer(track: Int, audioCodec: NegotiatedAudioCodec): String =
        if (audioCodec.isEvs) {
            "fmtp:$track ${audioCodec.evs?.fmtp ?: EvsSwbProfile.FMTP}"
        } else if (audioCodec.parameters != null) {
            "fmtp:$track ${audioCodec.parameters.substringAfter(' ')}"
        } else if (audioCodec.sdpCodecName == "AMR-WB") {
            "fmtp:$track octet-align=0;mode-change-capability=2;max-red=0"
        } else {
            "fmtp:$track mode-set=7;octet-align=0;max-red=0"
        }

    fun sdpBandwidthAsKbps(audioCodec: NegotiatedAudioCodec): Int =
        if (audioCodec.isEvs) ((audioCodec.evs?.txRates?.maxOrNull() ?: audioCodec.bitRate)/1000+64)
        else if(audioCodec.sdpCodecName == "AMR-WB") 88 else 38

    fun audioCodecExtras(audioCodec: NegotiatedAudioCodec): Map<String, String> =
        mapOf(
            "audio-codec" to audioCodec.name,
            "audio-codec-rate" to audioCodec.sampleRate.toString(),
        )

    private fun selectAmr(sdp: List<String>, wideband: Boolean): NegotiatedAudioCodec? {
        val candidates=SipAudioCodecSdpLogger.parseRemoteAudioCodecCandidates(sdp)
        val base=if(wideband) SipAudioCodecs.AMR_WB else SipAudioCodecs.AMR_NB
        val rates=if(wideband) EvsSession.IO_RATES else listOf(4750,5150,5900,6700,7400,7950,10200,12200)
        if(!candidates.any { it.codec=="TELEPHONE-EVENT" && it.rate==base.rtpClockRate }) return null
        for(c in candidates.filter { it.codec==base.sdpCodecName && it.rate==base.rtpClockRate && (it.channels==null || it.channels=="1") }) {
            val p=c.fmtp.substringAfter(' ',"").split(';').mapNotNull {
                val pair=it.trim().split('=',limit=2)
                if(pair.size==2) pair[0].trim().toLowerCase() to pair[1].trim() else null
            }.toMap()
            if((p["octet-align"] ?: "0") !in listOf("0","1") || (p["crc"] ?: "0")!="0" ||
                (p["robust-sorting"] ?: "0")!="0" || p["interleaving"]!=null) continue
            val modes=p["mode-set"]?.split(',')?.map { it.trim().toIntOrNull() ?: -1 } ?: rates.indices.toList()
            if(modes.isEmpty() || modes.any { it !in rates.indices }) continue
            val preferred=if(wideband) 2 else 7
            val mode=if(preferred in modes) preferred else modes.maxOrNull()!!
            return base.copy(bitRate=rates[mode],payloadId=c.payload,parameters=c.fmtp.takeIf { it.isNotBlank() })
        }
        return null
    }
    @Suppress("UNUSED_PARAMETER")
    fun selectIncomingSpeechCodecFromOffer(logTag: String, sdp: List<String>, context: String,
        amrWbMediaCodecAvailable: Boolean): NegotiatedAudioCodec = select(sdp,amrWbMediaCodecAvailable)
    @Suppress("UNUSED_PARAMETER")
    fun selectOutgoingSpeechCodecFromAnswer(logTag: String, sdp: List<String>, context: String,
        amrWbMediaCodecAvailable: Boolean): NegotiatedAudioCodec = select(sdp,amrWbMediaCodecAvailable)
    private fun select(sdp: List<String>, wideband: Boolean): NegotiatedAudioCodec {
        if(EvsSwbProfile.available()) EvsSwbProfile.select(sdp)?.let { return it }
        if(wideband) selectAmr(sdp,true)?.let { return it }
        return selectAmr(sdp,false) ?: SipAudioCodecs.AMR_NB
    }
}
