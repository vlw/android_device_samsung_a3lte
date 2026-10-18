package me.phh.sip

import android.telephony.Rlog
import java.util.UUID

/**
 * Adaptive outgoing INVITE session timer negotiation.
 *
 * Keep carrier/session-timer policy out of SipHandler: SipHandler owns the
 * dialog/socket state, while this helper owns Min-SE parsing, learning and
 * retry-header construction.
 */
data class SipInviteSessionTimer(
    val minSeSeconds: Int,
    val sessionExpiresSeconds: Int,
)

data class SipInviteRetryHeaders(
    val headers: SipHeadersMap,
    val cseqNumber: Int,
    val minSeSeconds: Int,
    val sessionExpiresSeconds: Int,
)

class SipInviteSessionTimerPolicy(
    private val tag: String,
) {
    private val learnedMinSeByRealm = mutableMapOf<String, Int>()

    private val defaultMinSeSeconds = 90
    private val defaultSessionExpiresSeconds = 1800
    private val minSeFloorSeconds = 90
    private val minSeCeilingSeconds = 86400

    fun currentForRealm(realm: String): SipInviteSessionTimer {
        val key = normalizedRealm(realm)
        val minSe = (learnedMinSeByRealm[key] ?: defaultMinSeSeconds)
            .coerceIn(minSeFloorSeconds, minSeCeilingSeconds)
        return SipInviteSessionTimer(
            minSeSeconds = minSe,
            sessionExpiresSeconds = maxOf(minSe, defaultSessionExpiresSeconds),
        )
    }

    fun buildRetryHeadersAfter422(
        realm: String,
        originalHeaders: SipHeadersMap,
        response: SipResponse,
    ): SipInviteRetryHeaders? {
        if (response.statusCode != 422) return null
        val failedCseq = firstHeader(response.headers, "cseq") ?: ""
        if (!failedCseq.contains("INVITE", ignoreCase = true)) return null

        val minSe = learnMinSeFrom422(realm, response) ?: return null
        val oldCseq = firstHeader(originalHeaders, "cseq") ?: failedCseq
        val retryCseqNumber = (oldCseq.substringBefore(" ").trim().toIntOrNull()
            ?: failedCseq.substringBefore(" ").trim().toIntOrNull()
            ?: 1) + 1
        val sessionExpires = maxOf(minSe, defaultSessionExpiresSeconds)

        val retryHeaders = regenerateSipViaBranch(
            originalHeaders -
                "cseq" -
                "CSeq" -
                "min-se" -
                "Min-Se" -
                "session-expires" -
                "Session-Expires" -
                "content-length" -
                "Content-Length" +
                """
                CSeq: $retryCseqNumber INVITE
                Min-SE: $minSe
                Session-Expires: $sessionExpires
                """.toSipHeadersMap()
        )

        return SipInviteRetryHeaders(
            headers = retryHeaders,
            cseqNumber = retryCseqNumber,
            minSeSeconds = minSe,
            sessionExpiresSeconds = sessionExpires,
        )
    }

    private fun learnMinSeFrom422(realm: String, response: SipResponse): Int? {
        val advertisedMinSe = parseDeltaSeconds(firstHeader(response.headers, "min-se"))
            ?: return null
        val minSe = advertisedMinSe.coerceIn(minSeFloorSeconds, minSeCeilingSeconds)
        val key = normalizedRealm(realm)
        val oldMinSe = learnedMinSeByRealm[key] ?: defaultMinSeSeconds
        if (minSe > oldMinSe) {
            learnedMinSeByRealm[key] = minSe
        }

        Rlog.w(
            tag,
            "Learned outgoing INVITE Min-SE=$minSe for realm=$key from " +
                "422 advertised=$advertisedMinSe old=$oldMinSe",
        )
        return minSe
    }

    private fun parseDeltaSeconds(value: String?): Int? =
        value
            ?.substringBefore(";")
            ?.trim()
            ?.toIntOrNull()

    private fun firstHeader(headers: SipHeadersMap, name: String): String? =
        headers[name]?.getOrNull(0)
            ?: headers[name.toLowerCase()]?.getOrNull(0)
            ?: headers.entries.firstOrNull { it.key.equals(name, ignoreCase = true) }?.value?.getOrNull(0)

    private fun normalizedRealm(realm: String): String = realm.trim().toLowerCase()

    private fun regenerateSipViaBranch(headers: SipHeadersMap): SipHeadersMap {
        val viaHeaders = headers["via"] ?: headers["Via"] ?: return headers
        val updatedViaHeaders = viaHeaders.map { via ->
            val newBranch = "z9hG4bK" + UUID.randomUUID().toString().replace("-", "").take(24)
            if (via.contains("branch=", ignoreCase = true)) {
                via.replace(Regex("branch=[^;\\s]+", RegexOption.IGNORE_CASE), "branch=$newBranch")
            } else {
                "$via;branch=$newBranch"
            }
        }
        return headers - "via" - "Via" + ("via" to updatedViaHeaders)
    }
}
