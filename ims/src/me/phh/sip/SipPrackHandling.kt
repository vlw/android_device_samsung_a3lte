package me.phh.sip

internal object SipPrackHandling {
    fun rackHeader(request: SipRequest): String =
        request.headers["rack"]!![0]

    fun receivedLog(rackHeader: String): String =
        "Received PRACK for $rackHeader"

    fun rackId(rackHeader: String): Int =
        rackHeader.split(" ")[0].toInt()

    fun okStatus(): Int = 200
}
