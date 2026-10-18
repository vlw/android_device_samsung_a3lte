// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

/** Stock ARM32 EVS Primary and AMR-WB IO. Never opens audio devices or the network. */
class StockEvsCodec @JvmOverloads constructor(val sampleRate: Int = 32000) : java.io.Closeable {
    companion object {
        init { System.loadLibrary("a3_evs_jni") }
        @JvmStatic private external fun nativeOpen(sampleRate: Int): Long
        @JvmStatic private external fun nativeClose(handle: Long)
        @JvmStatic private external fun nativeEncode(handle: Long, pcm: ByteArray, bitrate: Int, bandwidth: Int, dtx: Boolean, rfOffset: Int, rfHigh: Boolean): ByteArray
        @JvmStatic private external fun nativeDecode(handle: Long, frame: ByteArray): ByteArray
        val available: Boolean by lazy {
            try {
                StockEvsCodec().use { c ->
                    val frame = c.encode(ByteArray(1280))
                    frame.size == 34 && c.decode(frame).size == 1280
                }
            } catch (t: Throwable) { false }
        }
    }
    private var handle = nativeOpen(sampleRate)
    @JvmOverloads @Synchronized fun encode(pcm: ByteArray, bitrate: Int = 13200,
        bandwidth: Int = 2, dtx: Boolean = false, rfOffset: Int = 0,
        rfHigh: Boolean = true): ByteArray = nativeEncode(handle, pcm, bitrate, bandwidth, dtx, rfOffset, rfHigh)
    @Synchronized fun decode(frame: ByteArray): ByteArray = nativeDecode(handle, frame)
    @Synchronized override fun close() { nativeClose(handle); handle = 0 }
}
