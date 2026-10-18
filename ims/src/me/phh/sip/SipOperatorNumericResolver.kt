package me.phh.sip

import android.telephony.Rlog
import android.telephony.SubscriptionInfo
import android.telephony.TelephonyManager

object SipOperatorNumericResolver {
    private const val TAG = "PHH SipOperatorNumericResolver"

    /**
     * Resolve the home operator numeric used for carrier settings.
     *
     * TelephonyManager.simOperator can be briefly empty during early IMS/MMTel
     * startup even though SubscriptionInfo already has the active SIM MCC/MNC.
     * Prefer the SIM/home identity and only use the current network operator as
     * a last resort, because networkOperator can describe a visited PLMN while
     * roaming.
     */
    fun resolveHomeOperatorForIms(
        telephonyManager: TelephonyManager,
        activeSubscription: SubscriptionInfo,
        slotId: Int,
        subId: Int,
    ): String {
        val simOperator = telephonyManager.simOperator.orEmpty()
        if (isValidOperatorNumeric(simOperator)) {
            return simOperator
        }

        val subMcc = subscriptionStringField(activeSubscription, "getMccString")
        val subMnc = subscriptionStringField(activeSubscription, "getMncString")
        val fromSubscriptionString = subMcc + subMnc
        if (isValidOperatorNumeric(fromSubscriptionString)) {
            Rlog.w(
                TAG,
                "SIM operator empty/invalid for IMS; using subscription MCC/MNC " +
                    "$fromSubscriptionString slotId=$slotId subId=$subId " +
                    "mcc=$subMcc mnc=$subMnc sim='$simOperator'",
            )
            return fromSubscriptionString
        }

        val numericMcc = subscriptionIntField(activeSubscription, "getMcc")
        val numericMnc = subscriptionIntField(activeSubscription, "getMnc")
        if (numericMcc in 1..999 && numericMnc in 0..999) {
            val fromSubscriptionNumeric = numericMcc.toString().padStart(3, '0') +
                numericMnc.toString().padStart(if (numericMnc >= 100) 3 else 2, '0')
            if (isValidOperatorNumeric(fromSubscriptionNumeric)) {
                Rlog.w(
                    TAG,
                    "SIM operator empty/invalid for IMS; using numeric subscription MCC/MNC " +
                        "$fromSubscriptionNumeric slotId=$slotId subId=$subId " +
                        "mcc=$numericMcc mnc=$numericMnc sim='$simOperator'",
                )
                return fromSubscriptionNumeric
            }
        }

        val networkOperator = telephonyManager.networkOperator.orEmpty()
        if (isValidOperatorNumeric(networkOperator)) {
            Rlog.w(
                TAG,
                "SIM/subscription operator empty for IMS; using network operator as last resort " +
                    "$networkOperator slotId=$slotId subId=$subId sim='$simOperator'",
            )
            return networkOperator
        }

        Rlog.e(
            TAG,
            "No usable operator numeric for IMS slotId=$slotId subId=$subId " +
                "sim='$simOperator' network='$networkOperator' subscription=$activeSubscription",
        )
        throw IllegalStateException("No usable operator numeric for IMS slotId=$slotId subId=$subId")
    }

    private fun isValidOperatorNumeric(value: String): Boolean {
        return value.length in 5..6 && value.all { it.isDigit() }
    }

    private fun subscriptionStringField(subscriptionInfo: SubscriptionInfo, methodName: String): String {
        return try {
            subscriptionInfo.javaClass
                .getMethod(methodName)
                .invoke(subscriptionInfo) as? String ?: ""
        } catch (_: Throwable) {
            ""
        }
    }

    private fun subscriptionIntField(subscriptionInfo: SubscriptionInfo, methodName: String): Int {
        return try {
            subscriptionInfo.javaClass
                .getMethod(methodName)
                .invoke(subscriptionInfo) as? Int ?: -1
        } catch (_: Throwable) {
            -1
        }
    }
}
