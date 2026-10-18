// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.Rlog
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

internal object RtpPacketSender {
    fun send(
        tag: String,
        rtpSocket: DatagramSocket,
        bytes: ByteArray,
        remoteAddr: InetAddress,
        remotePort: Int,
        label: String,
    ): Boolean {
        return try {
            val packet = if (rtpSocket.isConnected) {
                // A connected DatagramSocket must be sent without an explicit packet
                // address on Android; otherwise libcore can throw
                // "connected address and packet address differ" even when the
                // dialog was only re-targeted by SDP/UPDATE during the call.
                DatagramPacket(bytes, bytes.size)
            } else {
                DatagramPacket(bytes, bytes.size, remoteAddr, remotePort)
            }

            rtpSocket.send(packet)
            true
        } catch (t: Throwable) {
            Rlog.e(
                tag,
                "Failed to send $label: connected=${rtpSocket.isConnected} " +
                    "socketRemote=${rtpSocket.inetAddress}:${rtpSocket.port} " +
                    "packetRemote=$remoteAddr:$remotePort",
                t,
            )
            false
        }
    }
}
