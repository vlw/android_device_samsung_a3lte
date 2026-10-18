//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.Rlog

object SipRemoteEndExtrasBuilder {
    fun build(
        logTag: String,
        callId: String,
        isBye: Boolean,
        isOutgoingCall: Boolean,
        outgoingConnectedNotified: Boolean,
    ): Map<String, String> {
        if (isBye && isOutgoingCall && !outgoingConnectedNotified) {
            Rlog.w(logTag, "Remote ended outgoing call before any RTP/media arrived; reporting as network rejection callId=$callId")
            return mapOf(
                "call-id" to callId,
                "statusCode" to "480",
                "statusString" to "No post-answer RTP before BYE",
                "remoteNoMediaRelease" to "true",
            )
        }

        return mapOf("call-id" to callId)
    }
}
