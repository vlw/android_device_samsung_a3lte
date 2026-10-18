//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.net.IpSecManager
import android.net.IpSecTransform

data class SipIpsecSettings(
    val clientSpiC: IpSecManager.SecurityParameterIndex,
    val clientSpiS: IpSecManager.SecurityParameterIndex,
    val serverSpiC: IpSecManager.SecurityParameterIndex? = null,
    val serverSpiS: IpSecManager.SecurityParameterIndex? = null,
    val serverInTransform: IpSecTransform? = null,
    val serverOutTransform: IpSecTransform? = null,
)
