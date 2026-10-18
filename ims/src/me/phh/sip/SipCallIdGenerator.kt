// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

internal object SipCallIdGenerator {
    fun generate(): SipHeadersMap {
        val callId = randomBytes(12).toHex()
        return mapOf("call-id" to listOf(callId))
    }
}
