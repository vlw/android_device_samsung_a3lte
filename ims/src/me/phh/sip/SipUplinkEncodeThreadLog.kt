package me.phh.sip


internal data class SipUplinkEncodeThreadStartState(
    val audioCodec: NegotiatedAudioCodec,
    val callId: String,
    val generation: Int,
)

internal object SipUplinkEncodeThreadLog {
    fun encodeThreadStarted(
        codecName: String,
        sampleRate: Int,
        callId: String,
        amrTrack: Int,
        remote: String,
        generation: Int,
    ): String =
        "Encode thread started: codec=$codecName/$sampleRate " +
            "callId=$callId amrTrack=$amrTrack " +
            "remote=$remote gen=$generation"

    fun rtpPacketLabel(sequenceNumber: Int): String =
        "RTP packet #$sequenceNumber"


    fun incomingSettleSilenceLabel(sequenceNumber: Int): String =
        "incoming RTP settle silence #$sequenceNumber"



    fun startState(
        call: SipHandler.Call,
        generation: Int,
    ): SipUplinkEncodeThreadStartState =
        SipUplinkEncodeThreadStartState(
            audioCodec = call.audioCodec,
            callId = call.callIdOrEmpty(),
            generation = generation,
        )

    fun noCurrentCallLog(reason: String): String =
        "callEncodeThread: no currentCall; not starting encoder reason=$reason"


}
