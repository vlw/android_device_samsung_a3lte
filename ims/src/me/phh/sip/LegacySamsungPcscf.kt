// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.net.InetAddresses
import android.os.SystemProperties
import java.net.InetAddress

// The stock RIL returns v6 data-call responses, which cannot carry P-CSCF.
// SetPcscfProperty publishes the network-provided addresses by CID - 1 instead.
// On this QMI device that index is the suffix of the active rmnet interface.
internal object LegacySamsungPcscf {
    fun read(
        interfaceName: String?,
        property: (String) -> String = { SystemProperties.get(it, "") },
    ): List<InetAddress> {
        val index = Regex("rmnet([0-9]+)").matchEntire(interfaceName ?: "")
            ?.groupValues?.get(1)?.toIntOrNull() ?: return emptyList()
        if (index !in 0..31) return emptyList()
        return listOf(6, 4).flatMap { family ->
            (0..2).mapNotNull { server ->
                val value = property("ril.pcscfv$family.gsm$index.$server")
                try {
                    InetAddresses.parseNumericAddress(value).takeUnless {
                        it.isAnyLocalAddress || it.isLoopbackAddress || it.isMulticastAddress
                    }
                } catch (_: IllegalArgumentException) {
                    null
                }
            }
        }.distinct()
    }
}
