// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.Rlog

private const val AMR_RTP_TAG = "PHH SipHandler"

internal data class AmrNbFrame(
    val ft: Int,
    val q: Int,
    val codecFrame: ByteArray,
)

private val amrNbSpeechBits = intArrayOf(95, 103, 118, 134, 148, 159, 204, 244, 39)

internal fun readPackedBits(src: ByteArray, startBit: Int, bitCount: Int): ByteArray {
        val out = ByteArray((bitCount + 7) / 8)
        for (i in 0 until bitCount) {
            val srcBit = startBit + i
            val bit = (src[srcBit / 8].toInt() ushr (7 - (srcBit % 8))) and 1
            if (bit != 0) {
                out[i / 8] = (out[i / 8].toInt() or (1 shl (7 - (i % 8)))).toByte()
            }
        }
        return out
    }

internal fun amrNbFrameFromBandwidthEfficientRtp(buf: ByteArray, length: Int): AmrNbFrame? {
    val payloadOffset = 12
    if (length < payloadOffset + 2) return null

    val ft = ((buf[payloadOffset].toUByte().toInt() and 0x07) shl 1) or
        ((buf[payloadOffset + 1].toUByte().toInt() ushr 7) and 0x01)
    val q = (buf[payloadOffset + 1].toUByte().toInt() ushr 6) and 0x01

    // FT=15 is No-Data. FT=8 is SID; pass it through because Android's AMR
    // decoder accepts normal AMR storage frames and this avoids decoder state gaps.
    if (ft == 15) return null
    if (ft !in amrNbSpeechBits.indices) {
        Rlog.w(AMR_RTP_TAG, "Unsupported AMR-NB RTP frame type ft=$ft length=$length")
        return null
    }

    val speechBits = amrNbSpeechBits[ft]
    val speechStartBit = payloadOffset * 8 + 10
    val availableBits = length * 8 - speechStartBit
    if (availableBits < speechBits) {
        Rlog.w(AMR_RTP_TAG, "Short AMR-NB RTP payload ft=$ft length=$length availableBits=$availableBits needed=$speechBits")
        return null
    }

    val frameHeader = ((ft shl 3) or (q shl 2)).toByte()
    return AmrNbFrame(
        ft = ft,
        q = q,
        codecFrame = byteArrayOf(frameHeader) + readPackedBits(buf, speechStartBit, speechBits),
    )
}
