//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.CellInfoGsm
import android.telephony.CellInfoLte
import android.telephony.CellInfoNr
import android.telephony.CellInfoWcdma
import android.telephony.Rlog
import android.telephony.TelephonyManager

object RegistrationCellInfoLogger {
    fun log(logTag: String, telephonyManager: TelephonyManager) {
        for (cell in telephonyManager.getAllCellInfo()) {
            if (cell is CellInfoLte) {
                val cellIdentity = cell.cellIdentity
                val cellSignalStrength = cell.cellSignalStrength
                Rlog.d(logTag, "LTE cell: ${cellIdentity.ci}, ${cellIdentity.pci}, ${cellIdentity.tac}, ${cellIdentity.mccString}, ${cellIdentity.mncString}, ${cellSignalStrength.dbm}")
            } else if (cell is CellInfoNr) {
                val cellIdentity = cell.cellIdentity
                val cellSignalStrength = cell.cellSignalStrength
                Rlog.d(logTag, "NR cell: ${cellIdentity.operatorAlphaLong}, ${cellIdentity.operatorAlphaShort}, ${cellIdentity}")
            } else if (cell is CellInfoWcdma) {
                val cellIdentity = cell.cellIdentity
                val cellSignalStrength = cell.cellSignalStrength
                Rlog.d(logTag, "WCDMA cell: ${cellIdentity.cid}, ${cellIdentity.lac}, ${cellIdentity.mccString}, ${cellIdentity.mncString}, ${cellSignalStrength.dbm}")
            } else if (cell is CellInfoGsm) {
                val cellIdentity = cell.cellIdentity
                val cellSignalStrength = cell.cellSignalStrength
                Rlog.d(logTag, "GSM cell: ${cellIdentity.cid}, ${cellIdentity.lac}, ${cellIdentity.mccString}, ${cellIdentity.mncString}, ${cellSignalStrength.dbm}")
            }
        }
    }
}
