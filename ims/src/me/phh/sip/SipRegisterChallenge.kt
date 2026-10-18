//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

data class SipRegisterChallenge(
    val nonceB64: String,
    val realm: String,
    val opaque: String?,
    val qop: String?,
)

object SipRegisterChallengeParser {
    fun parse(response: SipResponse, fallbackRealm: String): SipRegisterChallenge {
        val (wwwAuthenticateType, wwwAuthenticateParams) =
            response.headers["www-authenticate"]!![0].getAuthValues()
        require(wwwAuthenticateType == "Digest")

        return SipRegisterChallenge(
            nonceB64 = wwwAuthenticateParams["nonce"]!!,
            // Use the realm from the 401 challenge for H1 and the Authorization realm= field,
            // as required by RFC 2617. Carriers often differ from the subscriber's own realm.
            realm = wwwAuthenticateParams["realm"] ?: fallbackRealm,
            opaque = wwwAuthenticateParams["opaque"],
            qop = wwwAuthenticateParams["qop"],
        )
    }
}
