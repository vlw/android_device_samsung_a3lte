//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipSecurityClientHeader {
    fun build(
        ipsecSettings: SipIpsecSettings,
        clientPort: Int,
        serverPort: Int,
        algs: List<String> = listOf("hmac-sha-1-96", "hmac-md5-96"),
        ealgs: List<String> = listOf("null", "aes-cbc"),
    ): String {
        fun secClient(alg: String, ealg: String) =
            "ipsec-3gpp;prot=esp;mod=trans;spi-c=${ipsecSettings.clientSpiC.spi};spi-s=${ipsecSettings.clientSpiS.spi};port-c=$clientPort;port-s=$serverPort;ealg=$ealg;alg=$alg"

        val secClients = algs.flatMap { alg -> ealgs.map { ealg -> secClient(alg, ealg) } }
        return "Security-Client: ${secClients.joinToString(", ")}"
    }
}
