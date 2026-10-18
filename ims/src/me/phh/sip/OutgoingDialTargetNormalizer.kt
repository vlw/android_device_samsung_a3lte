//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.PhoneNumberUtils
import android.telephony.Rlog
import android.telephony.SubscriptionInfo
import android.telephony.TelephonyManager

object OutgoingDialTargetNormalizer {
    fun normalize(
        rawPhoneNumber: String,
        activeSubscription: SubscriptionInfo,
        telephonyManager: TelephonyManager,
        logTag: String,
    ): String {
        val stripped = PhoneNumberUtils.stripSeparators(rawPhoneNumber).trim()

        if (stripped.isEmpty()) return stripped

        // Keep MMI/USSD, post-dial sequences and carrier/service codes in their
        // original form. They are not normal public E.164 dial targets.
        if (stripped.any { it == '*' || it == '#' || it == ',' || it == ';' }) {
            return stripped
        }

        // Already a global TEL URI target.
        if (stripped.startsWith("+")) {
            return stripped
        }

        // Android's framework can pass international-prefix form unchanged
        // (for example 0049...). IMS cores behave more consistently with +E.164.
        if (stripped.startsWith("00") &&
            stripped.length > 2 &&
            stripped.drop(2).all { it.isDigit() }) {
            return "+" + stripped.drop(2)
        }

        // Only rewrite obvious national public numbers. Short codes like 110,
        // 112, 116117, mailbox codes, etc. must keep their original form.
        if (!stripped.all { it.isDigit() } || !stripped.startsWith("0")) {
            return stripped
        }

        val countryIso = listOf(
            activeSubscription.countryIso,
            telephonyManager.simCountryIso,
            telephonyManager.networkCountryIso,
        ).firstOrNull { !it.isNullOrBlank() }

        val e164 = countryIso?.let { iso ->
            PhoneNumberUtils.formatNumberToE164(stripped, iso.toUpperCase())
        }

        if (e164 == null) {
            Rlog.w(
                logTag,
                "Could not normalize outgoing dial target to E.164: " +
                    "raw=$rawPhoneNumber stripped=$stripped countryIso=$countryIso",
            )
            return stripped
        }

        return e164
    }
}
