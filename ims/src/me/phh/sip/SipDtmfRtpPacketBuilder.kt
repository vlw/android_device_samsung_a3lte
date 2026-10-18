//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipDtmfRtpPacketBuilder {
    fun buildTelephoneEventPacket(
        payloadType: Int,
        sequenceNumber: Int,
        timestamp: Int,
        event: Int,
        duration: Int,
        repeatIndex: Int,
        volume: Int = 10,
    ): ByteArray {
        val marker = if (repeatIndex == 0) 0x80 else 0x00
        val end = if (repeatIndex >= 3) 0x80 else 0x00

        val rtpHeader = byteArrayOf(
            0x80.toByte(),
            (marker or payloadType).toByte(),
            (sequenceNumber shr 8).toByte(),
            (sequenceNumber and 0xff).toByte(),
            (timestamp shr 24).toByte(),
            ((timestamp shr 16) and 0xff).toByte(),
            ((timestamp shr 8) and 0xff).toByte(),
            (timestamp and 0xff).toByte(),
            0x03,
            0x00,
            0xd2.toByte(),
            0x00,
        )

        val payload = byteArrayOf(
            event.toByte(),
            (end or volume).toByte(),
            (duration shr 8).toByte(),
            (duration and 0xff).toByte(),
        )

        return rtpHeader + payload
    }
}
