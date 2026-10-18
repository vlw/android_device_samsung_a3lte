package me.phh.sip

/**
 * Generic REGISTER negotiation helpers.
 *
 * Some IMS cores challenge the initial home-realm REGISTER with a registrar
 * realm that also has to be used as the protected REGISTER authority, for
 * example realm="ims.example.com" while our default target is the 3GPP home
 * network domain.
 *
 * Other networks expose a shorter operator auth realm only.  A1 HR/21910 uses
 * realm="vip.hr" for AKA, but still expects the REGISTER Request-URI and Digest
 * URI to stay on sip:ims.mnc010.mcc219.3gppnetwork.org.  O2 Germany/26203
 * similarly challenges with an EPC AKA realm, epc.mnc007.mcc262.3gppnetwork.org,
 * but rejects an authenticated REGISTER sent to that EPC realm.  Therefore the
 * helper below only promotes challenged realms that look like IMS registrar
 * domains, not arbitrary 3GPP/EPC auth realms.
 */
object SipRegisterNegotiationPolicy {
    data class RegisterRealmDecision(
        val targetRealm: String,
        val candidateRealm: String,
        val canonicalRealm: String,
        val forcedCanonical: Boolean,
    ) {
        val usesPromotedChallengeRealm: Boolean
            get() = !targetRealm.equals(canonicalRealm, ignoreCase = true)

        val hasPromotedCandidate: Boolean
            get() = !candidateRealm.equals(canonicalRealm, ignoreCase = true)
    }

    fun registerRealmDecision(
        defaultRealm: String,
        challengeRealm: String?,
        preferCanonicalAfterPromoted494: Boolean,
    ): RegisterRealmDecision {
        val candidateRealm = challengedRegistrarRealm(
            defaultRealm = defaultRealm,
            challengeRealm = challengeRealm,
        )
        val targetRealm =
            if (preferCanonicalAfterPromoted494) defaultRealm else candidateRealm

        return RegisterRealmDecision(
            targetRealm = targetRealm,
            candidateRealm = candidateRealm,
            canonicalRealm = defaultRealm,
            forcedCanonical = preferCanonicalAfterPromoted494,
        )
    }

    fun shouldRetryCanonicalAfterPromoted494(
        statusCode: Int?,
        decision: RegisterRealmDecision,
        alreadyPreferCanonical: Boolean,
    ): Boolean =
        statusCode == 494 &&
            decision.usesPromotedChallengeRealm &&
            !alreadyPreferCanonical

    private val SAFE_HOST_RE = Regex("^[A-Za-z0-9.-]+$")
    /*
     * Some networks challenge IMS REGISTER with an EPC/AKA realm such as
     * epc.mncXXX.mccYYY.3gppnetwork.org. That realm is valid for the
     * Authorization realm, but it is not necessarily the SIP registrar
     * authority. Do not promote it to the protected REGISTER Request-URI or
     * digest URI, otherwise carriers that expect the IMS home domain there can
     * reject the authenticated REGISTER with 403.
     */
    private fun isEpcAuthRealm(realm: String): Boolean =
        realm.startsWith("epc.", ignoreCase = true) &&
            realm.endsWith(".3gppnetwork.org", ignoreCase = true)


    fun challengedRegistrarRealm(defaultRealm: String, challengeRealm: String?): String {
        val candidate = challengeRealm
            ?.trim()
            ?.trim('"')
            ?.toLowerCase()
            ?: return defaultRealm

        if (candidate.isEmpty()) return defaultRealm
        if (candidate.equals(defaultRealm, ignoreCase = true)) return defaultRealm
        if (!candidate.contains('.')) return defaultRealm
        if (!SAFE_HOST_RE.matches(candidate)) return defaultRealm
        if (candidate.startsWith(".") || candidate.endsWith(".")) return defaultRealm
        if (candidate.contains("..")) return defaultRealm

        /*
         * The WWW-Authenticate realm is not always the REGISTER authority.
         * A1 HR/21910 challenges with realm="vip.hr", but the REGISTER
         * Request-URI and Digest URI must stay on the home IMS domain:
         * sip:ims.mnc010.mcc219.3gppnetwork.org.  Rewriting that final
         * protected REGISTER to sip:vip.hr makes the network reject it with
         * 494 Security Agreement Required.
         *
         * Only promote the challenged realm to REGISTER authority when it
         * looks like an actual IMS registrar domain.  Do not promote EPC
         * authentication realms such as epc.mnc007.mcc262.3gppnetwork.org:
         * O2 Germany/26203 uses that realm for AKA, but still expects the
         * protected REGISTER Request-URI and Digest URI to stay on the
         * original ims.mnc003.mcc262.3gppnetwork.org home domain.
         *
         * This keeps SingTel-style realm="ims.singtel.com" / eims.* cases
         * working, while treating operator/EPC auth realms such as "vip.hr"
         * or "epc.*" as Digest realm only.
         */
        val looksLikeRegistrarRealm =
            candidate.startsWith("ims.") ||
                candidate.startsWith("eims.")

        if (!looksLikeRegistrarRealm) return defaultRealm

        return candidate
    }

    fun selectedSecurityClientHeader(
        securityServerParams: Map<String, String>,
        ipsecSettings: SipIpsecSettings,
        clientPort: Int,
        serverPort: Int,
    ): String? {
        val selectedAlg = securityServerParams["alg"] ?: return null
        val selectedEalg = securityServerParams["ealg"] ?: "null"

        return SipSecurityClientHeader.build(
            ipsecSettings = ipsecSettings,
            clientPort = clientPort,
            serverPort = serverPort,
            algs = listOf(selectedAlg),
            ealgs = listOf(selectedEalg),
        )
    }
}
