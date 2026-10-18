package me.phh.sip

internal object SipAckHandling {
    fun receivedLog(
        callId: String,
        currentCallId: String?,
        outgoing: Boolean?,
    ): String =
        "Received ACK for call-id=$callId current=$currentCallId outgoing=$outgoing"

    fun startIncomingMediaLog(): String =
        "Starting incoming media threads from final ACK"

    fun incomingMediaAlreadyStartedLog(): String =
        "Incoming media threads already started before final ACK"

    fun okStatus(): Int = 0
}
