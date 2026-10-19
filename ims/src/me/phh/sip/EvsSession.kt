// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

/** Negotiated constraints are immutable; CMR only changes the bounded transmit state. */
data class EvsTransmitMode(val bitrate: Int, val bandwidth: Int, val io: Boolean,
    val rfOffset: Int = 0, val rfHigh: Boolean = true)

class EvsSession(
    val txRates: Set<Int>, val rxRates: Set<Int>,
    val txBands: Set<Int>, val rxBands: Set<Int>, val ioModes: Set<Int>,
    val initialIo: Boolean, val hfOnly: Boolean, val cmr: Int,
    val txDtx: Boolean, val rxDtx: Boolean, val rfAllowed: Boolean,
    val modePeriod: Int, val modeNeighbor: Boolean, val fmtp: String,
    val payload: Int = 102, initialRf: Int = 0,
) {
    companion object {
        val PRIMARY_RATES = listOf(5900,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000)
        val IO_RATES = listOf(6600,8850,12650,14250,15850,18250,19850,23050,23850)
        // Local transmit ceiling: full SWB up to 64 kbit/s. The negotiated
        // br/bw still bound it; it is never exceeded by CMR either.
        const val MAX_TX_RATE = 64000
        const val PREFERRED_TX_BAND = 2
        fun validPair(rate: Int, band: Int): Boolean = when (band) {
            0 -> rate in PRIMARY_RATES && rate <= 24400
            1 -> rate in PRIMARY_RATES
            2 -> rate in PRIMARY_RATES && rate >= 9600
            3 -> rate in PRIMARY_RATES && rate >= 16400
            else -> false
        }
    }
    private fun primary(rateLimit: Int, bandLimit: Int): EvsTransmitMode? {
        val choices = txRates.filter { it <= rateLimit && txBands.any { b -> b <= bandLimit && validPair(it,b) } }
        val rate = choices.maxOrNull() ?: return null
        val band = txBands.filter { it <= bandLimit && validPair(rate,it) }.maxOrNull() ?: return null
        return EvsTransmitMode(rate,band,false)
    }
    // Start at the highest negotiated rate (up to 64 kbit/s SWB) instead of
    // 13.2: many networks never send a CMR, which kept the uplink at 13.2.
    private val initialPrimary = primary(MAX_TX_RATE,PREFERRED_TX_BAND)
        ?: primary(MAX_TX_RATE,3) ?: primary(128000,3)
    @Volatile var transmit: EvsTransmitMode = if (initialIo) {
        val mode = if (2 in ioModes) 2 else ioModes.maxOrNull()!!
        EvsTransmitMode(IO_RATES[mode],1,true)
    } else initialPrimary!!.let { if (initialRf > 0) it.copy(bitrate=13200,rfOffset=initialRf) else it }
        private set
    private var pending: EvsTransmitMode? = null
    private var frames = 0L
    @Synchronized fun encodeMode(): EvsTransmitMode {
        val requested = pending
        if (requested != null && (!requested.io || frames % modePeriod == 0L)) {
            var next = requested
            if (requested.io && transmit.io && modeNeighbor) {
                val modes = ioModes.sorted()
                val oldIndex = modes.indexOf(IO_RATES.indexOf(transmit.bitrate))
                val newIndex = modes.indexOf(IO_RATES.indexOf(requested.bitrate))
                val adjacent = newIndex.coerceIn(oldIndex-1,oldIndex+1).coerceIn(0,modes.lastIndex)
                next = requested.copy(bitrate=IO_RATES[modes[adjacent]])
            }
            transmit = next
            if (next == requested) pending = null
        }
        frames++
        return transmit
    }
    @Synchronized fun receiveCmr(value: Int) {
        if (value == 255) return
        val type = (value shr 4) and 7
        val mode = value and 15
        if (cmr == -1 && (!transmit.io || type != 1)) return
        val requested = when (type) {
            1 -> {
                val selected = ioModes.filter { it <= mode && mode <= 8 }.maxOrNull() ?: return
                EvsTransmitMode(IO_RATES[selected],1,true)
            }
            0,2,3,4 -> {
                val rates = listOf(5900,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000)
                val band = when(type) { 0 -> 0; 2 -> 1; 3 -> 2; else -> 3 }
                val rate = rates.getOrNull(mode) ?: return
                // An out-of-session request is ignored, rather than silently
                // changing to an unrelated bandwidth/rate.
                if (band !in txBands || rate !in txRates || !validPair(rate,band)) return
                primary(minOf(rate,maxOf(MAX_TX_RATE,txRates.minOrNull()!!)),band) ?: return
            }
            5,6 -> {
                val band = if (type == 5) 1 else 2
                if (!rfAllowed || mode !in 0..7 || 13200 !in txRates || band !in txBands) return
                EvsTransmitMode(13200,band,false,listOf(2,3,5,7)[mode and 3],mode >= 4)
            }
            else -> return
        }
        pending = requested
    }
    fun accepts(toc: Int): Boolean {
        val ft = toc and 15
        val io = toc and 0x20 != 0
        if (io) return ft in ioModes || ft == 9 && rxDtx || ft in 14..15
        if (ft == 12) return rxDtx
        if (ft in 14..15) return true
        val rate = EvsFrames.PRIMARY_BITRATES.getOrNull(ft) ?: return false
        val allowed = rate in rxRates || (rate == 2800 || rate == 7200) && 5900 in rxRates
        return allowed && rxBands.any { validPair(if(rate==2800) 5900 else rate,it) }
    }
    fun codec(): NegotiatedAudioCodec {
        val band = transmit.bandwidth
        return SipAudioCodecs.EVS_SWB.copy(name="EVS-${listOf("NB","WB","SWB","FB")[band]}",
            sampleRate=48000,bitRate=transmit.bitrate,evs=this,payloadId=payload)
    }
}

object EvsFrames {
    val PRIMARY_BITS = intArrayOf(56,144,160,192,264,328,488,640,960,1280,1920,2560,48,-1,0,0)
    val IO_BITS = intArrayOf(132,177,253,285,317,365,397,461,477,40,-1,-1,-1,-1,0,0)
    val PRIMARY_BITRATES = intArrayOf(2800,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000)
    fun bits(toc: Int): Int? {
        if (toc and 0xc0 != 0 || toc and 0x30 == 0x10) return null
        return (if (toc and 0x20 != 0) IO_BITS else PRIMARY_BITS)[toc and 15].takeIf { it >= 0 }
    }
    fun size(toc: Int): Int? = bits(toc)?.let { 1+(it+7)/8 }
    fun speech(toc: Int): Boolean = (toc and 15) < if (toc and 0x20 != 0) 9 else 12
}
