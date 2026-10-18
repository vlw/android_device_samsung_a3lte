// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.telephony.Rlog
import android.telephony.TelephonyManager
import android.telephony.ims.stub.ImsRegistrationImplBase.REGISTRATION_TECH_IWLAN
import android.telephony.ims.stub.ImsRegistrationImplBase.REGISTRATION_TECH_LTE
import java.net.Inet6Address
import java.net.InetAddress

internal sealed class ImsNetworkEndpointResolution {
    data class Success(
        val pcscfAddr: InetAddress,
        val localAddr: InetAddress,
    ) : ImsNetworkEndpointResolution()

    object WaitingForPcscf : ImsNetworkEndpointResolution()
    object NoLocalAddress : ImsNetworkEndpointResolution()
}

internal object ImsNetworkState {
    fun registrationTechName(tech: Int): String =
        when (tech) {
            REGISTRATION_TECH_IWLAN -> "IWLAN"
            REGISTRATION_TECH_LTE -> "LTE"
            else -> "unknown($tech)"
        }

    fun detectRegistrationTech(
        connectivityManager: ConnectivityManager,
        network: Network?,
        lp: LinkProperties,
    ): Int {
        val iface = lp.interfaceName ?: ""
        if (iface.startsWith("ipsec", ignoreCase = true)) {
            return REGISTRATION_TECH_IWLAN
        }

        val caps = if (network != null) {
            try {
                connectivityManager.getNetworkCapabilities(network)
            } catch (_: Throwable) {
                null
            }
        } else {
            null
        }

        return if (caps?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true) {
            REGISTRATION_TECH_IWLAN
        } else {
            REGISTRATION_TECH_LTE
        }
    }

    fun getPcscfServers(lp: LinkProperties): List<InetAddress> {
        val advertised = (lp.javaClass.getMethod("getPcscfServers").invoke(lp) as List<*>)
            .filterIsInstance<InetAddress>()
            .sortedBy { if (it is Inet6Address) 0 else 1 }
        return advertised.ifEmpty { LegacySamsungPcscf.read(lp.interfaceName) }
    }

    fun getImsLocalAddress(lp: LinkProperties): InetAddress? {
        return lp.linkAddresses
            .map { it.address }
            .filter { !it.isAnyLocalAddress && !it.isLoopbackAddress }
            .sortedBy { if (it is Inet6Address) 0 else 1 }
            .firstOrNull()
    }

    fun resolveEndpoint(
        tag: String,
        lp: LinkProperties,
        network: Network,
        mnc: String,
        mcc: String,
        preferredPcscf: InetAddress? = null,
    ): ImsNetworkEndpointResolution {
        val pcscfs = getPcscfServers(lp)
        val pcscf = preferredPcscf ?: if (pcscfs.isNotEmpty()) {
            pcscfs[0]
        } else {
            // RIL did not provide P-CSCF via LinkProperties. Try standard
            // 3GPP DNS discovery (TS 23.003 §13.2): resolve the well-known
            // IMS domain for this PLMN.
            val dnsFallback = try {
                network.getAllByName("ims.mnc${mnc}.mcc${mcc}.pub.3gppnetwork.org").firstOrNull()
            } catch (t: Throwable) {
                null
            } ?: try {
                network.getAllByName("ims.mnc${mnc}.mcc${mcc}.3gppnetwork.org").firstOrNull()
            } catch (t: Throwable) {
                null
            } ?: android.os.SystemProperties
                .get("persist.ims.pcscf_fallback", "")
                .takeIf { it.isNotEmpty() }
                ?.let {
                    try {
                        network.getAllByName(it).firstOrNull()
                    } catch (t: Throwable) {
                        null
                    }
                }

            if (dnsFallback != null) {
                Rlog.w(tag, "No P-CSCF from RIL, using fallback: $dnsFallback")
                dnsFallback
            } else {
                Rlog.w(tag, "No P-CSCF and all fallbacks failed, waiting for onLinkPropertiesChanged")
                return ImsNetworkEndpointResolution.WaitingForPcscf
            }
        }

        val localAddr = getImsLocalAddress(lp)
        if (localAddr == null) {
            Rlog.w(tag, "No usable local address on IMS link properties")
            return ImsNetworkEndpointResolution.NoLocalAddress
        }

        return ImsNetworkEndpointResolution.Success(pcscf, localAddr)
    }

    fun ratName(rat: Int): String =
        when (rat) {
            TelephonyManager.NETWORK_TYPE_LTE -> "LTE"
            TelephonyManager.NETWORK_TYPE_NR -> "NR"
            TelephonyManager.NETWORK_TYPE_IWLAN -> "IWLAN"
            TelephonyManager.NETWORK_TYPE_EDGE -> "EDGE"
            TelephonyManager.NETWORK_TYPE_GPRS -> "GPRS"
            TelephonyManager.NETWORK_TYPE_GSM -> "GSM"
            TelephonyManager.NETWORK_TYPE_UMTS -> "UMTS"
            TelephonyManager.NETWORK_TYPE_HSPA -> "HSPA"
            TelephonyManager.NETWORK_TYPE_HSDPA -> "HSDPA"
            TelephonyManager.NETWORK_TYPE_HSUPA -> "HSUPA"
            TelephonyManager.NETWORK_TYPE_UNKNOWN -> "UNKNOWN"
            else -> "rat($rat)"
        }

    fun isRatReadyForImsNetworkRequest(
        tag: String,
        telephonyManager: TelephonyManager,
    ): Boolean {
        val dataRat = try {
            telephonyManager.dataNetworkType
        } catch (t: Throwable) {
            TelephonyManager.NETWORK_TYPE_UNKNOWN
        }

        val voiceRat = try {
            telephonyManager.voiceNetworkType
        } catch (t: Throwable) {
            TelephonyManager.NETWORK_TYPE_UNKNOWN
        }

        val ready =
            dataRat == TelephonyManager.NETWORK_TYPE_LTE ||
                dataRat == TelephonyManager.NETWORK_TYPE_NR ||
                dataRat == TelephonyManager.NETWORK_TYPE_IWLAN ||
                voiceRat == TelephonyManager.NETWORK_TYPE_LTE ||
                voiceRat == TelephonyManager.NETWORK_TYPE_NR

        Rlog.d(
            tag,
            "IMS network request RAT gate: data=${ratName(dataRat)} voice=${ratName(voiceRat)} ready=$ready",
        )

        return ready
    }
}
