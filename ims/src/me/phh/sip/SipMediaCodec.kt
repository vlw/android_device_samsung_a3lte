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
) {
    companion object {
        fun android(codec: MediaCodec) = SipMediaCodec(codec, null, false)
        fun evs(encode: Boolean) = SipMediaCodec(null, StockEvsCodec(), encode)
    }
    private val input = ByteBuffer.allocate(65536)
    private val output = ArrayDeque<ByteArray>()
    private var partialPcm = ByteArray(0)
    fun dequeueInputBuffer(timeoutUs: Long): Int = androidCodec?.dequeueInputBuffer(timeoutUs) ?: 0
    fun getInputBuffer(index: Int): ByteBuffer? = if (androidCodec != null) androidCodec.getInputBuffer(index) else input
    fun queueInputBuffer(index: Int, offset: Int, size: Int, timeUs: Long, flags: Int) {
        if (androidCodec != null) { androidCodec.queueInputBuffer(index,offset,size,timeUs,flags); return }
        require(offset >= 0 && size >= 0 && offset + size <= input.capacity())
        val bytes = input.array().copyOfRange(offset,offset+size)
        if (encode) {
            val all = partialPcm + bytes
            var pos = 0
            while (all.size - pos >= 1280) {
                output.add(evs!!.encode(all.copyOfRange(pos,pos+1280)))
                pos += 1280
            }
            partialPcm = all.copyOfRange(pos,all.size)
        } else { output.add(evs!!.decode(bytes)) }
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
