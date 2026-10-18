package me.phh.sip

internal object SipOutgoingInviteRetryPolicy {
    fun responseWarnsIllegalSdp(
        response: SipResponse,
        policy: SipInviteFailurePolicy = SipInviteFailurePolicy(),
    ): Boolean {
        if (response.statusCode !in policy.retryIllegalSdpStatusCodes) return false
        val warningValues = response.headers.entries
            .filter { it.key.equals("warning", ignoreCase = true) }
            .flatMap { it.value }
        return warningValues.any { warning ->
            policy.retryIllegalSdpWarningSubstrings.any { token ->
                warning.contains(token, ignoreCase = true)
            }
        }
    }



    fun notRetryingAfter422TwiceLog(callId: String): String =
        "Not retrying outgoing INVITE after 422 twice callId=$callId"

    fun retryInviteAfter422(
        destination: String,
        retryHeaders: SipHeadersMap,
        body: ByteArray,
    ): SipRequest =
        SipRequest(
            SipMethod.INVITE,
            destination,
            retryHeaders,
            body,
        )

    fun desiredNextCseqAfter422Retry(retryCseqNumber: Int): Int =
        retryCseqNumber + 1

    fun retryAfter422Log(
        callId: String,
        minSeSeconds: Int,
        sessionExpiresSeconds: Int,
        cseqNumber: Int,
    ): String =
        "Retrying outgoing INVITE after 422 with Min-SE=$minSeSeconds " +
            "Session-Expires=$sessionExpiresSeconds " +
            "callId=$callId cseq=$cseqNumber"

    fun notRetryingIllegalSdpAfterCancelLog(callId: String): String =
        "Not retrying outgoing INVITE after illegal SDP because CANCEL was already sent callId=$callId"

    fun notRetryingIllegalSdpTwiceLog(callId: String): String =
        "Not retrying outgoing INVITE after illegal SDP twice callId=$callId"

    fun originalInviteCseq(headers: SipHeadersMap): Int {
        val oldCseqHeader = headers.entries
            .firstOrNull { it.key.equals("cseq", ignoreCase = true) }
            ?.value
            ?.getOrNull(0)
            ?: "1 INVITE"
        return oldCseqHeader.substringBefore(" ").trim().toIntOrNull() ?: 1
    }

    fun nextInviteCseq(oldCseq: Int): Int = oldCseq + 1

    fun illegalSdpRetryInvite(
        destination: String,
        retryHeaders: SipHeadersMap,
        retryBody: ByteArray,
    ): SipRequest =
        SipRequest(
            SipMethod.INVITE,
            destination,
            retryHeaders,
            retryBody,
        )

    fun illegalSdpRetryLog(
        callId: String,
        oldCseq: Int,
        retryCseq: Int,
        oldBytes: Int,
        retryBytes: Int,
        dualSimDebugContext: String,
    ): String =
        "Retrying outgoing INVITE after 400 illegal SDP with conservative AMR-NB offer " +
            "callId=$callId oldCseq=$oldCseq retryCseq=$retryCseq " +
            "oldBytes=$oldBytes retryBytes=$retryBytes " +
            dualSimDebugContext

    fun illegalSdpRetryWriteLabel(): String =
        "SipHandler illegal-sdp retry INVITE"

    private fun removeSipHeaderToken(
        headers: SipHeadersMap,
        headerName: String,
        token: String,
    ): SipHeadersMap {
        val values = headers.entries
            .filter { it.key.equals(headerName, ignoreCase = true) }
            .flatMap { it.value }
        if (values.isEmpty()) return headers

        val filteredValues = values.mapNotNull { value ->
            val keptTokens = value.split(',')
                .map { it.trim() }
                .filter { it.isNotEmpty() && !it.equals(token, ignoreCase = true) }
            if (keptTokens.isEmpty()) null else keptTokens.joinToString(", ")
        }
        val strippedHeaders = headers.filterKeys { !it.equals(headerName, ignoreCase = true) }
        return if (filteredValues.isEmpty()) {
            strippedHeaders
        } else {
            strippedHeaders + (headerName.toLowerCase() to filteredValues)
        }
    }

    fun retryHeaders(
        headers: SipHeadersMap,
        retryCseq: Int,
    ): SipHeadersMap {
        var retryHeaders = headers.filterKeys {
            !it.equals("cseq", ignoreCase = true) &&
                !it.equals("content-length", ignoreCase = true)
        } + ("cseq" to listOf("$retryCseq INVITE"))
        retryHeaders = removeSipHeaderToken(retryHeaders, "supported", "precondition")
        retryHeaders = removeSipHeaderToken(retryHeaders, "require", "precondition")
        return retryHeaders
    }

    fun conservativeAmrNbRetryBody(
        originalBody: ByteArray,
        localHost: String,
    ): ByteArray {
        val originalLines = originalBody
            .toString(Charsets.US_ASCII)
            .split(Regex("\r?\n"))
            .map { it.trim() }
            .filter { it.isNotEmpty() }

        fun line(prefix: String): String? = originalLines.firstOrNull { it.startsWith(prefix) }
        val ipType = if (localHost.contains(':')) "IP6" else "IP4"
        val audioPort = line("m=audio ")?.split(Regex("\\s+"))?.getOrNull(1) ?: "0"

        val retryLines = listOf(
            line("v=") ?: "v=0",
            line("o=") ?: "o=- 1 2 IN $ipType $localHost",
            line("s=") ?: "s=phh voice call",
            line("c=") ?: "c=IN $ipType $localHost",
            "b=AS:38",
            "b=RS:0",
            "b=RR:0",
            line("t=") ?: "t=0 0",
            "m=audio $audioPort RTP/AVP 97 100",
            "b=AS:38",
            "b=RS:0",
            "b=RR:0",
            "a=ptime:20",
            "a=maxptime:240",
            "a=rtpmap:97 AMR/8000/1",
            "a=fmtp:97 mode-change-capability=2;octet-align=0;max-red=0",
            "a=rtpmap:100 telephone-event/8000",
            "a=fmtp:100 0-15",
            "a=sendrecv",
        )
        return (retryLines.joinToString("\r\n") + "\r\n").toByteArray(Charsets.US_ASCII)
    }
}
