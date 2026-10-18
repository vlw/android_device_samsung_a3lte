//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.telephony.SubscriptionInfo
import android.telephony.SubscriptionManager
import android.telephony.TelephonyManager

data class SipSubscriptionContext(
    val activeSubscription: SubscriptionInfo,
    val subId: Int,
    val telephonyManager: TelephonyManager,
    val imei: String,
) {
    companion object {
        fun resolve(
            ctxt: Context,
            telephonyManager: TelephonyManager,
            slotId: Int,
            requestedSubId: Int,
        ): SipSubscriptionContext {
            val subscriptionManager = ctxt.getSystemService(SubscriptionManager::class.java)
            val activeSubscriptions = subscriptionManager.activeSubscriptionInfoList.orEmpty()
            val activeSubscription = activeSubscriptions.firstOrNull {
                it.simSlotIndex == slotId && it.subscriptionId == requestedSubId
            } ?: activeSubscriptions.firstOrNull {
                it.subscriptionId == requestedSubId
            } ?: activeSubscriptions.firstOrNull {
                it.simSlotIndex == slotId
            } ?: throw IllegalStateException(
                "No active subscription for slotId=$slotId requestedSubId=$requestedSubId"
            )

            val subId = activeSubscription.subscriptionId
            return SipSubscriptionContext(
                activeSubscription = activeSubscription,
                subId = subId,
                telephonyManager = telephonyManager.createForSubscriptionId(subId),
                imei = telephonyManager.getImei(activeSubscription.simSlotIndex),
            )
        }
    }
}
