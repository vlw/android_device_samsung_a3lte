//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import java.net.DatagramSocket
import java.net.InetAddress

object SipUplinkSilenceRtpSender {
    fun sendNoDataPacket(
        logTag: String,
        audioCodec: NegotiatedAudioCodec,
        payloadType: Int,
        sequenceNumber: Int,
        timestamp: Int,
        rtpSocket: DatagramSocket,
        remoteAddr: InetAddress,
        remotePort: Int,
        label: String,
    ): Boolean {
        // TS 26.445 A.2.2.1.2: do not send standalone EVS NO_DATA packets.
        // The pre-answer/microphone-settle pacer still waits without RTP output.
        if (audioCodec.isEvs) return true
        val packet = SipAmrRtpPayload.buildNoDataRtpPacket(
            audioCodec = audioCodec,
            payloadType = payloadType,
            sequenceNumber = sequenceNumber,
            timestamp = timestamp,
        )
        return RtpPacketSender.send(
            tag = logTag,
            rtpSocket = rtpSocket,
            bytes = packet,
            remoteAddr = remoteAddr,
            remotePort = remotePort,
            label = label,
        )
    }
}
