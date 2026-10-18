package me.phh.sip

import android.telephony.Rlog
import java.io.OutputStream


internal object SipUpdateDialogValidator {
    fun nonCurrentDialogLog(
        requestCallId: String,
        requestCseq: String,
        currentCallId: String?,
    ): String =
        "Rejecting UPDATE for non-current dialog: callId=$requestCallId cseq=$requestCseq current=$currentCallId"

    fun nonCurrentDialogStatus(): Int = 481


    fun isSdpUpdate(request: SipRequest): Boolean =
        request.headers["content-type"]
            ?.getOrNull(0)
            ?.startsWith("application/sdp", ignoreCase = true) == true &&
            request.body.isNotEmpty()

}

internal object SipUpdateResponseBuilder {
    fun okWithoutSdp(
        request: SipRequest,
        requestCallId: String,
    ): SipResponse {
        return SipResponse(
            statusCode = 200,
            statusString = "OK",
            headersParam = request.headers.filter { (k, _) ->
                k in listOf("cseq", "via", "from", "to", "call-id")
            } + """
                Supported: 100rel, replaces, timer
                Call-ID: $requestCallId
                Content-Length: 0
            """.toSipHeadersMap(),
            autofill = false,
        )
    }

    fun okWithSdp(
        request: SipRequest,
        callId: String,
        answerSdp: ByteArray,
    ): SipResponse {
        return SipResponse(
            statusCode = 200,
            statusString = "OK",
            headersParam = request.headers.filter { (k, _) ->
                k in listOf("cseq", "via", "from", "to", "call-id")
            } + """
                Content-Type: application/sdp
                Supported: 100rel, replaces, timer
                Require: precondition
                Call-ID: $callId
            """.toSipHeadersMap(),
            body = answerSdp,
        )
    }
}

internal object SipUpdateResponseWriter {

    fun writeOkWithoutSdp(
        request: SipRequest,
        requestCallId: String,
        updateResponseWriter: OutputStream,
        logTag: String,
    ) {
        val reply = SipUpdateResponseBuilder.okWithoutSdp(
            request = request,
            requestCallId = requestCallId,
        )
        writeReply(
            updateResponseWriter = updateResponseWriter,
            reply = reply,
            logTag = logTag,
        )
    }

    fun writeReply(
        updateResponseWriter: OutputStream,
        reply: SipResponse,
        logTag: String,
    ) {
        Rlog.d(logTag, "Replying to UPDATE with $reply")
        synchronized(updateResponseWriter) {
            updateResponseWriter.write(reply.toByteArray())
        }
    }


    fun writeSdpAnswerAndRingingIfNeeded(
        request: SipRequest,
        call: SipHandler.Call,
        updateResponseWriter: OutputStream,
        updatedCallId: String,
        answerSdp: ByteArray,
        logTag: String,
    ) {
        val reply = SipUpdateResponseBuilder.okWithSdp(
            request = request,
            callId = updatedCallId,
            answerSdp = answerSdp,
        )
        writeReply(
            updateResponseWriter = updateResponseWriter,
            reply = reply,
            logTag = logTag,
        )

        sendIncomingRingingIfNeeded(
            call = call,
            updateResponseWriter = updateResponseWriter,
            logTag = logTag,
        )
    }

    fun sendIncomingRingingIfNeeded(
        call: SipHandler.Call,
        updateResponseWriter: OutputStream,
        logTag: String,
    ) {
        if (call.outgoing) {
            return
        }

        val myHeaders2 = call.callHeaders - "rseq" - "content-type" - "require"
        val msg2 = SipResponse(
            statusCode = 180,
            statusString = "Ringing",
            headersParam = myHeaders2,
        )
        Rlog.d(logTag, "Sending $msg2")
        synchronized(updateResponseWriter) {
            updateResponseWriter.write(msg2.toByteArray())
        }
    }
}
