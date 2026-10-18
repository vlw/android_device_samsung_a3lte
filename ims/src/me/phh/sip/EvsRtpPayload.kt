// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

/** TS 26.445 Annex A. Pure packet formatting, no native calls or socket access. */
object EvsRtpPayload {
    data class Packet(val frames: List<ByteArray>, val cmr: Int?, val timestamp: Int)
    private val compactPrimary = EvsFrames.PRIMARY_BITS.withIndex().filter { it.index<=12 }
        .associate { (it.value+7)/8 to it.index }
    private val compactIo = EvsFrames.IO_BITS.take(9).withIndex().associate { (it.value+10)/8 to it.index }
    private val protectedSizes = compactPrimary.keys + compactIo.keys
    private val compactCmrModes = listOf(0,1,2,4,5,7,8)
    private fun bit(data: ByteArray,index: Int): Int = (data[index/8].toInt() ushr (7-index%8)) and 1
    private fun setBit(data: ByteArray,index: Int,value: Int) { if(value!=0) data[index/8]=(data[index/8].toInt() or (1 shl (7-index%8))).toByte() }
    fun payload(frame: ByteArray, session: EvsSession): ByteArray? {
        if(frame.isEmpty() || EvsFrames.size(frame[0].toInt() and 255)!=frame.size) return null
        val toc=frame[0].toInt() and 255
        val ft=toc and 15
        if(ft==15) return null // standalone NO_DATA is never transmitted
        val io=toc and 32!=0
        val compact = !session.hfOnly && session.cmr!=1
        if(compact && !io && ft<=12) return frame.copyOfRange(1,frame.size)
        if(compact && io && ft<9) {
            val count=EvsFrames.IO_BITS[ft]
            val out=ByteArray((count+10)/8)
            out[0]=0xe0.toByte() // three-bit CMR=none
            for(i in 1 until count) setBit(out,i+2,bit(frame,8+i))
            setBit(out,count+2,bit(frame,8)) // d(0) follows d(K-1)
            return out
        }
        val cmrPresent=io || session.cmr==1
        var out=(if(cmrPresent) byteArrayOf(0xff.toByte()) else ByteArray(0))+frame
        if(!session.hfOnly && !(out.size==7 && io && ft==9)) {
            while(out.size in protectedSizes) out+=0.toByte()
        }
        return out
    }
    fun parse(packet: ByteArray, length: Int, session: EvsSession): Packet? {
        if(length<12 || length>packet.size || packet[0].toInt() and 0xc0!=0x80) return null
        if(packet[1].toInt() and 127 != session.payload) return null
        var start=12+4*(packet[0].toInt() and 15)
        var end=length
        if(start>end) return null
        if(packet[0].toInt() and 16!=0) {
            if(start+4>end) return null
            start+=4+4*((packet[start+2].toInt() and 255)*256+(packet[start+3].toInt() and 255))
            if(start>end) return null
        }
        if(packet[0].toInt() and 32!=0) {
            val padding=packet[end-1].toInt() and 255
            if(padding==0 || padding>end-start) return null
            end-=padding
        }
        if(start>=end) return null
        val payload=packet.copyOfRange(start,end)
        var cmr: Int?=null
        val frames=mutableListOf<ByteArray>()
        if(!session.hfOnly && (payload.size!=7 || payload[0].toInt() and 128==0) && payload.size in compactPrimary) {
            val ft=compactPrimary[payload.size]!!
            frames+=byteArrayOf(ft.toByte())+payload
        } else if(!session.hfOnly && payload.size in compactIo) {
            val ft=compactIo[payload.size]!!
            val count=EvsFrames.IO_BITS[ft]
            val frame=ByteArray(1+(count+7)/8)
            frame[0]=(0x30 or ft).toByte()
            for(i in 1 until count) setBit(frame,8+i,bit(payload,i+2))
            setBit(frame,8,bit(payload,count+2))
            val request=(payload[0].toInt() ushr 5) and 7
            if(request<7) cmr=0x90 or compactCmrModes[request]
            frames+=frame
        } else {
            var pos=0
            if(payload[0].toInt() and 128!=0) cmr=payload[pos++].toInt() and 255
            if(session.cmr==1 && cmr==null) return null
            val tocs=mutableListOf<Int>()
            do {
                if(pos>=payload.size || tocs.size>=12) return null
                val toc=payload[pos++].toInt() and 255
                if(toc and 128!=0 || EvsFrames.bits(toc and 0x3f)==null) return null
                tocs+=toc and 0x3f
            } while(toc and 64!=0)
            for(toc in tocs) {
                if(toc and 32!=0 && cmr==null) return null // IO Header-Full requires CMR
                val size=EvsFrames.size(toc)!!-1
                if(pos+size>payload.size) return null
                frames+=byteArrayOf(toc.toByte())+payload.copyOfRange(pos,pos+size)
                pos+=size
            }
            if(session.hfOnly && pos!=payload.size) return null
            if(payload.copyOfRange(pos,payload.size).any { it!=0.toByte() }) return null
        }
        if(frames.isEmpty() || frames.any { !session.accepts(it[0].toInt() and 255) }) return null
        val timestamp=(packet[4].toInt() shl 24) or ((packet[5].toInt() and 255) shl 16) or
            ((packet[6].toInt() and 255) shl 8) or (packet[7].toInt() and 255)
        return Packet(frames,cmr,timestamp)
    }
}
