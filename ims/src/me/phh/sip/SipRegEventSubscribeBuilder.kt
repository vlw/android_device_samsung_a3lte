//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipRegEventSubscribeBuilder {
    fun build(
        mySip: String,
        myTel: String,
        commonHeaders: SipHeadersMap,
        socket: SipConnection,
        serverPort: Int,
        imei: String,
        accessNetworkInfo: String? = null,
    ): SipRequest {
        val contactTel = SipContactHeaders.mmtelContact(
            userPart = myTel,
            localEndpoint = SipContactHeaders.localEndpoint(socket, serverPort),
            transport = SipContactHeaders.transport(socket),
            sipInstance = SipContactHeaders.sipInstanceFromImei(imei),
        )

        return SipRequest(
            SipMethod.SUBSCRIBE,
            mySip,
            commonHeaders +
                """
                Contact: $contactTel
                P-Preferred-Identity: <$mySip>
                Event: reg
                Expires: 7200
                Supported: sec-agree
                Require: sec-agree
                Proxy-Require: sec-agree
                Allow: INVITE, ACK, CANCEL, BYE, UPDATE, REFER, NOTIFY, INFO, MESSAGE, PRACK, OPTIONS
                Accept: application/reginfo+xml
                ${accessNetworkInfo?.let { "P-Access-Network-Info: $it" }.orEmpty()}
                """.toSipHeadersMap(),
        )
    }
}
