package me.phh.sip

import android.telephony.Rlog

/**
 * Short-lived IMS-SMS fallback learning.
 *
 * If the carrier rejects the SIP MESSAGE itself before RP_ACK/RP_ERROR, Android
 * should get a fallback signal instead of repeatedly hammering IMS SMS on the
 * same realm. Successful carriers continue using SMS-over-IMS normally.
 */
class SipSmsFallbackPolicy(
    private val tag: String,
    private val policy: SipSmsPolicy = SipSmsPolicy(),
) {
    private val fallbackUntilByRealm = mutableMapOf<String, Long>()

    fun learnFromSipMessageFailure(realm: String, statusCode: Int) {
        // Only learn fallback from terminal MESSAGE-level failures before an RP
        // result arrives. These cover common SBC/core denials and unsupported
        // MESSAGE shapes without permanently disabling SMS-over-IMS.
        if (statusCode !in policy.fallbackSipStatusCodes) return

        val key = normalizedRealm(realm)
        val until = System.currentTimeMillis() + policy.fallbackCooldownMs
        fallbackUntilByRealm[key] = until
        Rlog.w(
            tag,
            "Learning IMS SMS fallback for realm=$key after SIP MESSAGE status=$statusCode " +
                "until=$until",
        )
    }

    fun shouldBypass(realm: String): Boolean {
        val key = normalizedRealm(realm)
        val until = fallbackUntilByRealm[key] ?: return false
        val now = System.currentTimeMillis()
        if (now >= until) {
            fallbackUntilByRealm.remove(key)
            return false
        }
        Rlog.w(tag, "Bypassing IMS SMS for realm=$key due to learned SIP MESSAGE failure until=$until")
        return true
    }

    private fun normalizedRealm(realm: String): String = realm.trim().toLowerCase()
}
