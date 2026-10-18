// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.AccessNetworkConstants
import android.telephony.CellIdentityLte
import android.telephony.CellInfoLte
import android.telephony.NetworkRegistrationInfo
import android.telephony.ServiceState
import android.telephony.TelephonyManager
import java.util.Locale

object SipAccessNetworkInfo {
    @JvmStatic
    fun lte(operator: String?, tac: Int, ci: Int, tdd: Boolean): String? {
        if (operator == null || !operator.matches(Regex("[0-9]{5,6}")) ||
            tac !in 0..65535 || ci !in 0..0xfffffff) return null
        val cell = String.format(Locale.US, "%s%04x%07x", operator, tac, ci)
        val access = if (tdd) "3GPP-E-UTRAN-TDD" else "3GPP-E-UTRAN-FDD"
        return "$access;utran-cell-id-3gpp=$cell"
    }

    fun current(manager: TelephonyManager): String? = try {
        val state = manager.serviceState
        val cells = listOf(NetworkRegistrationInfo.DOMAIN_PS, NetworkRegistrationInfo.DOMAIN_CS)
            .mapNotNull { state?.getNetworkRegistrationInfo(it, AccessNetworkConstants.TRANSPORT_TYPE_WWAN)
                ?.cellIdentity as? CellIdentityLte }
            // Legacy Samsung RIL can return a zero-filled PS identity while
            // the CS registration contains the actual LTE cell.
            .filterNot { it.tac == 0 && it.ci == 0 }
        val operator = state?.operatorNumeric ?: manager.networkOperator
        val tdd = state?.duplexMode == ServiceState.DUPLEX_MODE_TDD
        cells.mapNotNull { lte(operator, it.tac, it.ci, tdd) }.firstOrNull()
            ?: manager.allCellInfo.orEmpty().filterIsInstance<CellInfoLte>()
                .filter { it.isRegistered }
                .filterNot { it.cellIdentity.tac == 0 && it.cellIdentity.ci == 0 }
                .mapNotNull { lte(operator, it.cellIdentity.tac, it.cellIdentity.ci, tdd) }.firstOrNull()
    } catch (_: Exception) { null }
}
