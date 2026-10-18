//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipRtpPacketParser {
    fun payloadType(packet: ByteArray): Int =
        (packet[1].toUByte().toInt() and 0x7f)
}
