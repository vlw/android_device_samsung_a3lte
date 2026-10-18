//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.net.IpSecAlgorithm
import android.net.IpSecManager
import android.net.IpSecTransform
import java.net.InetAddress

data class SipIpsecTransforms(
    val builder: IpSecTransform.Builder,
    val serverInTransform: IpSecTransform,
    val serverOutTransform: IpSecTransform,
)

object SipIpsecTransformBuilder {
    fun build(
        ctxt: Context,
        pcscfAddr: InetAddress,
        localAddr: InetAddress,
        clientSpiS: IpSecManager.SecurityParameterIndex,
        serverSpiC: IpSecManager.SecurityParameterIndex,
        securityServerParams: Map<String, String>,
        integrityKey: ByteArray,
        cipherKey: ByteArray,
    ): SipIpsecTransforms {
        val encryptionAlgorithm = securityServerParams["ealg"] ?: "null"
        val (authenticationAlgorithm, hmacKey) =
            if (securityServerParams["alg"] == "hmac-sha-1-96") {
                // sha-1-96 MAC key must be 160 bits, pad IK.
                IpSecAlgorithm.AUTH_HMAC_SHA1 to integrityKey + ByteArray(4)
            } else {
                IpSecAlgorithm.AUTH_HMAC_MD5 to integrityKey
            }

        val builder = IpSecTransform.Builder(ctxt)
            .setAuthentication(IpSecAlgorithm(authenticationAlgorithm, hmacKey, 96))
            .also {
                if (encryptionAlgorithm == "aes-cbc") {
                    it.setEncryption(IpSecAlgorithm(IpSecAlgorithm.CRYPT_AES_CBC, cipherKey))
                }
            }

        return SipIpsecTransforms(
            builder = builder,
            serverInTransform = builder.buildTransportModeTransform(pcscfAddr, clientSpiS),
            serverOutTransform = builder.buildTransportModeTransform(localAddr, serverSpiC),
        )
    }
}
