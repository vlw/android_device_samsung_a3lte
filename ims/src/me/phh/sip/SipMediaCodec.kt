// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.media.MediaCodec
import java.nio.ByteBuffer
import java.util.ArrayDeque

/** Keep the existing Android AMR path; adapt the stock synchronous EVS codec. */
class SipMediaCodec private constructor(
    private val androidCodec: MediaCodec?,
    private val evs: StockEvsCodec?,
    private val encode: Boolean,
    private val profile: NegotiatedAudioCodec? = null,
) {
    companion object {
        fun android(codec: MediaCodec) = SipMediaCodec(codec, null, false)
        fun evs(profile: NegotiatedAudioCodec, encode: Boolean) = SipMediaCodec(null, StockEvsCodec(profile.sampleRate), encode, profile)
        private const val LATE_PACKET_QUEUE_LIMIT = 3 // 60 ms
    }
    private val input = ByteBuffer.allocate(65536)
    private val output = ArrayDeque<ByteArray>()
    private var partialPcm = ByteArray(0)
    private val frameBytes get() = (profile?.sampleRate ?: 32000)/50*2
    private var lastTimestamp: Int? = null // playout position, advanced by concealment
    private var lastReceivedTimestamp: Int? = null
    private var lastToc: Int? = null
    @Synchronized fun evsConcealment(): ByteArray? {
        if (evs==null || encode) return null
        val toc=lastToc ?: return null
        val silence=(toc and 15)==12 && toc and 32==0 || (toc and 15)==9 && toc and 32!=0
        val pcm=evs.decode(byteArrayOf(((toc and 0x30) or if(silence) 15 else 14).toByte()))
        // Concealment consumed one media slot. A later packet must not insert
        // the same missing frame again or play an already concealed timestamp.
        lastTimestamp=lastTimestamp?.plus(320)
        return pcm
    }
    @Synchronized fun queueEvsPacket(packet: ByteArray, length: Int,
        pcmQueue: java.util.concurrent.ArrayBlockingQueue<ByteArray>) {
        val session=profile?.evs ?: return
        val received=EvsRtpPayload.parse(packet,length,session) ?: return
        val end=received.timestamp+320*(received.frames.size-1)
        val receivedBefore=lastReceivedTimestamp
        if(receivedBefore!=null && received.timestamp-receivedBefore<=0) return // duplicate/reordered
        lastReceivedTimestamp=end
        received.cmr?.let { session.receiveCmr(it) }
        val previous=lastTimestamp
        // Playout concealed this slot while the packet was slightly late. Do
        // not drop it: that made every following packet "late" too and muted
        // the downlink for seconds. Play it unless enough audio is queued.
        val late=previous!=null && received.timestamp-previous<=0
        if(late && pcmQueue.size>=LATE_PACKET_QUEUE_LIMIT) return
        fun offer(pcm: ByteArray) {
            if(!pcmQueue.offer(pcm)) { pcmQueue.poll(); pcmQueue.offer(pcm) }
        }
        if(previous!=null && !late && lastToc?.let { EvsFrames.speech(it) }==true) {
            val lost=((received.timestamp-previous)/320-1).coerceIn(0,12)
            repeat(lost) { evsConcealment()?.let { offer(it) } }
        }
        for(frame in received.frames) {
            offer(evs!!.decode(frame))
            lastToc=frame[0].toInt() and 255
        }
        if(previous==null || end-previous>0) lastTimestamp=end
    }
    fun dequeueInputBuffer(timeoutUs: Long): Int = androidCodec?.dequeueInputBuffer(timeoutUs) ?: 0
    fun getInputBuffer(index: Int): ByteBuffer? = if (androidCodec != null) androidCodec.getInputBuffer(index) else input
    fun queueInputBuffer(index: Int, offset: Int, size: Int, timeUs: Long, flags: Int) {
        if (androidCodec != null) { androidCodec.queueInputBuffer(index,offset,size,timeUs,flags); return }
        require(offset >= 0 && size >= 0 && offset + size <= input.capacity())
        val bytes = input.array().copyOfRange(offset,offset+size)
        if (encode) {
            val all = partialPcm + bytes
            var pos = 0
            while (all.size - pos >= frameBytes) {
                val session=profile!!.evs!!
                val mode=session.encodeMode()
                output.add(evs!!.encode(all.copyOfRange(pos,pos+frameBytes),mode.bitrate,mode.bandwidth,
                    session.txDtx,mode.rfOffset,mode.rfHigh))
                pos += frameBytes
            }
            partialPcm = all.copyOfRange(pos,all.size)
        } else { output.add(evs!!.decode(bytes)); lastToc=bytes[0].toInt() and 255 }
    }
    fun dequeueOutputBuffer(info: MediaCodec.BufferInfo, timeoutUs: Long): Int {
        if (androidCodec != null) return androidCodec.dequeueOutputBuffer(info, timeoutUs)
        val frame = output.peek() ?: return MediaCodec.INFO_TRY_AGAIN_LATER
        info.set(0,frame.size,0,0)
        return 0
    }
    fun getOutputBuffer(index: Int): ByteBuffer? = androidCodec?.getOutputBuffer(index) ?: output.peek()?.let { ByteBuffer.wrap(it) }
    fun releaseOutputBuffer(index: Int, render: Boolean) {
        if (androidCodec != null) androidCodec.releaseOutputBuffer(index,render) else output.remove()
    }
    fun stop() { androidCodec?.stop() }
    fun release() { if (androidCodec != null) androidCodec.release() else evs?.close(); output.clear() }
}
