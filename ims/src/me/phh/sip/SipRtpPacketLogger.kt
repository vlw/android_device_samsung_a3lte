//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.Rlog
import java.net.DatagramPacket

object SipRtpPacketLogger {
    private fun shouldLogReceivedPacket(receivedCount: Int): Boolean =
        receivedCount <= 10 || receivedCount % 50 == 0

    fun logReceivedPacket(
        logTag: String,
        receivedCount: Int,
        packet: DatagramPacket,
        payloadType: Int,
        frameType: Int,
        codecFrameSize: Int,
    ) {
        if (!shouldLogReceivedPacket(receivedCount)) return

        Rlog.d(
            logTag,
            "Received RTP packet #$receivedCount: " +
                "from=${packet.address}:${packet.port} " +
                "length=${packet.length} pt=$payloadType ft=$frameType " +
                "codecBytes=$codecFrameSize"
        )
    }
}
