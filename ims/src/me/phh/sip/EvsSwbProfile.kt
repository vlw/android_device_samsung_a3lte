// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

/** EVS SDP negotiation (TS 26.445 A.3). Retained name for the original SWB port. */
object EvsSwbProfile {
    // Omit hf-only, dtx and evs-mode-switch: the answer may select either value.
    const val FMTP = "br=5.9-128;bw=nb-fb;mode-set=0,1,2,3,4,5,6,7,8;ch-aw-recv=0;max-red=0"
    fun available(): Boolean = try { StockEvsCodec.available } catch (t: Throwable) { false }
    private fun params(fmtp: String): Map<String,String> {
        val raw = fmtp.trim().removePrefix("a=")
        val text = if (raw.startsWith("fmtp:",true)) raw.substringAfter(' ', "") else raw
        val result = linkedMapOf<String,String>()
        for (item in text.split(';')) {
            if (item.isBlank()) continue
            val pair = item.trim().split('=',limit=2)
            require(pair.size == 2 && pair[0].isNotBlank())
            val key = pair[0].trim().toLowerCase()
            val value = pair[1].trim().toLowerCase()
            require(value.isNotEmpty() && (result[key] == null || result[key] == value))
            result[key] = value
        }
        return result
    }
    private fun rates(value: String?): Set<Int> {
        if (value == null) return EvsSession.PRIMARY_RATES.toSet()
        val range = value.split('-').map {
            val number=it.toDoubleOrNull() ?: error("Invalid rate")
            require(number.isFinite() && number*1000==(number*1000).toInt().toDouble())
            (number*1000).toInt()
        }
        require(range.size in 1..2 && range.all { it in EvsSession.PRIMARY_RATES })
        require(range.size == 1 || range[0] < range[1])
        return EvsSession.PRIMARY_RATES.filter { it in range.first()..range.last() }.toSet()
    }
    private fun bands(value: String?): Set<Int> = when(value) {
        null,"nb-fb" -> setOf(0,1,2,3)
        "nb-swb" -> setOf(0,1,2)
        "nb-wb" -> setOf(0,1)
        "nb" -> setOf(0); "wb" -> setOf(1); "swb" -> setOf(2); "fb" -> setOf(3)
        else -> error("Invalid bandwidth")
    }
    fun negotiate(fmtp: String, payload: Int = 102): EvsSession? = try {
        val p = params(fmtp)
        for ((common,send,recv) in listOf(Triple("br","br-send","br-recv"),Triple("bw","bw-send","bw-recv"))) {
            if (p[common] != null) require(listOf(send,recv).all { p[it] == null || p[it] == p[common] })
        }
        fun flag(key: String, default: Int): Int = (p[key]?.toIntOrNull() ?: if (p[key]==null) default else -2).also { require(it in 0..1) }
        val io = flag("evs-mode-switch",0)==1
        val hf = flag("hf-only",0)==1
        val dtx = flag("dtx",1)==1
        val recvDtx = flag("dtx-recv",if(dtx) 1 else 0)==1
        if(p["dtx"]!=null && p["dtx-recv"]!=null) require(dtx==recvDtx)
        require((p["ch-send"] ?: "1")=="1" && (p["ch-recv"] ?: "1")=="1")
        val cmr = p["cmr"]?.toIntOrNull() ?: if (p["cmr"]==null) 0 else -2
        require(cmr in -1..1)
        val rf = p["ch-aw-recv"]?.toIntOrNull() ?: if(p["ch-aw-recv"]==null) 0 else -2
        require(rf in listOf(-1,0,2,3,5,7))
        val modes = p["mode-set"]?.split(',')?.map { it.trim().toIntOrNull() ?: -1 }?.toSet() ?: (0..8).toSet()
        require(modes.isNotEmpty() && modes.all { it in 0..8 })
        require((p["mode-change-capability"] ?: "2")=="2")
        val period = p["mode-change-period"]?.toIntOrNull() ?: if(p["mode-change-period"]==null) 1 else -1
        require(period in 1..2)
        val neighbor = flag("mode-change-neighbor",0)==1
        val txRates = rates(p["br"] ?: p["br-recv"])
        val rxRates = rates(p["br"] ?: p["br-send"])
        val txBands = bands(p["bw"] ?: p["bw-recv"])
        val rxBands = bands(p["bw"] ?: p["bw-send"])
        require(txRates.any { r -> txBands.any { EvsSession.validPair(r,it) } })
        require(rxRates.any { r -> rxBands.any { EvsSession.validPair(r,it) } })
        if(rf>0) require(!io && 13200 in txRates && txBands.any { it in 1..2 })
        // Mirror directional constraints into the local answer. Do not echo
        // peer-send as local-send, or replace an offered immutable flag.
        val answer = linkedMapOf<String,String>()
        for ((key,value) in p) {
            val localKey = when(key) { "br-send" -> "br-recv"; "br-recv" -> "br-send"; "bw-send" -> "bw-recv"; "bw-recv" -> "bw-send"; else -> key }
            if(key in listOf("br","br-send","br-recv","bw","bw-send","bw-recv","hf-only","evs-mode-switch","dtx","mode-set","mode-change-period","mode-change-neighbor","mode-change-capability","cmr")) answer[localKey]=value
        }
        // We support CNG both ways; peer dtx-recv controls local transmit only.
        if(p["dtx"]==null) answer["dtx-recv"]="1"
        else if(p["dtx-recv"]!=null) answer["dtx-recv"]=if(dtx) "1" else "0"
        answer["ch-aw-recv"]="0"
        answer["max-red"]="0"
        EvsSession(txRates,rxRates,txBands,rxBands,modes,io,hf,cmr,recvDtx,dtx,rf>=0,period,neighbor,
            answer.entries.joinToString(";") { "${it.key}=${it.value}" },payload,rf.coerceAtLeast(0))
    } catch (t: IllegalArgumentException) { null } catch (t: IllegalStateException) { null }
    @Suppress("UNUSED_PARAMETER")
    fun compatible(fmtp: String, answer: Boolean): Boolean = negotiate(fmtp)!=null
    fun select(sdp: List<String>): NegotiatedAudioCodec? {
        val c = SipAudioCodecSdpLogger.parseRemoteAudioCodecCandidates(sdp)
        if(!c.any { it.codec=="TELEPHONE-EVENT" && it.rate==16000 && it.offeredOrder!=Int.MAX_VALUE }) return null
        val maxptime = sdp.firstOrNull { it.startsWith("a=maxptime:") }?.substringAfter(':')?.toIntOrNull()
        if(maxptime!=null && maxptime<20) return null
        return c.asSequence().filter { it.codec=="EVS" && it.rate==16000 && it.payload in 96..127 &&
            it.offeredOrder!=Int.MAX_VALUE && (it.channels==null || it.channels=="1") }
            .mapNotNull { negotiate(it.fmtp,it.payload)?.codec() }.firstOrNull()
    }
    @Suppress("UNUSED_PARAMETER")
    fun selectable(sdp: List<String>, answer: Boolean): Boolean = select(sdp)!=null
}
