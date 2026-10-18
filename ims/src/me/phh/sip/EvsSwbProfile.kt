// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

/** The only EVS profile implemented by this port; do not advertise other modes. */
object EvsSwbProfile {
    const val FMTP = "br=13.2;bw=swb;hf-only=1;evs-mode-switch=0;dtx=0;dtx-recv=0;cmr=-1;max-red=0"
    fun available(): Boolean = try { StockEvsCodec.available } catch (t: Throwable) { false }
    private fun params(fmtp: String): Map<String,String> = fmtp.substringAfter(' ', "")
        .split(';').mapNotNull {
            val pair = it.trim().split('=', limit=2)
            if (pair.size == 2) pair[0].trim().toLowerCase() to pair[1].trim().toLowerCase() else null
        }.toMap()
    private fun rateAllows(value: String?): Boolean {
        if (value == null) return true
        val range = value.split('-').map { it.toDoubleOrNull() ?: return false }
        return when (range.size) { 1 -> range[0] == 13.2; 2 -> range[0] <= 13.2 && range[1] >= 13.2; else -> false }
    }
    private fun bandwidthAllows(value: String?): Boolean {
        if (value == null) return true
        val bands = listOf("nb","wb","swb","fb")
        val range = value.split('-').map { bands.indexOf(it) }
        return range.all { it >= 0 } && when(range.size) {
            1 -> range[0] == 2; 2 -> range[0] <= 2 && range[1] >= 2; else -> false
        }
    }
    fun compatible(fmtp: String, answer: Boolean): Boolean {
        val p = params(fmtp)
        if ((p["evs-mode-switch"] ?: "0") != "0") return false
        if ((p["cmr"] ?: "0") !in listOf("-1","0")) return false
        if ((p["ch-send"] ?: "1") != "1" || (p["ch-recv"] ?: "1") != "1") return false
        if (!listOf("br","br-send","br-recv").all { rateAllows(p[it]) }) return false
        if (!listOf("bw","bw-send","bw-recv").all { bandwidthAllows(p[it]) }) return false
        if (answer) {
            // The answer must preserve the offered fixed primary SWB profile.
            if (p["br"] != "13.2" || p["bw"] != "swb" || p["hf-only"] != "1" || p["cmr"] != "-1") return false
            if (p["dtx"] != "0" || (p["dtx-recv"] ?: "0") != "0") return false
        }
        return true
    }
    fun selectable(sdp: List<String>, answer: Boolean): Boolean {
        val c = SipAudioCodecSdpLogger.parseRemoteAudioCodecCandidates(sdp)
        val evs = c.firstOrNull { it.codec == "EVS" && it.rate == 16000 } ?: return false
        if (evs.payload !in 96..127 || evs.offeredOrder == Int.MAX_VALUE ||
            evs.channels != null && evs.channels != "1" || !compatible(evs.fmtp,answer)) return false
        // Respect a peer which cannot receive our 20 ms packetization.
        val ptime = sdp.firstOrNull { it.startsWith("a=ptime:") }?.substringAfter(':')?.toIntOrNull()
        val maxptime = sdp.firstOrNull { it.startsWith("a=maxptime:") }?.substringAfter(':')?.toIntOrNull()
        if (ptime != null && ptime != 20 || maxptime != null && maxptime < 20) return false
        return c.any { it.codec == "TELEPHONE-EVENT" && it.rate == 16000 && it.offeredOrder != Int.MAX_VALUE }
    }
}
