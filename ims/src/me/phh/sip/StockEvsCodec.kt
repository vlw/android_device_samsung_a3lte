// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

/** Stock ARM32 EVS Primary 13.2 SWB. Never opens audio devices or the network. */
class StockEvsCodec : java.io.Closeable {
    companion object {
        init { System.loadLibrary("a3_evs_jni") }
        @JvmStatic private external fun nativeOpen(): Long
        @JvmStatic private external fun nativeClose(handle: Long)
        @JvmStatic private external fun nativeEncode(handle: Long, pcm: ByteArray): ByteArray
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
    private var handle = nativeOpen()
    @Synchronized fun encode(pcm: ByteArray): ByteArray = nativeEncode(handle, pcm)
    @Synchronized fun decode(frame: ByteArray): ByteArray = nativeDecode(handle, frame)
    @Synchronized override fun close() { nativeClose(handle); handle = 0 }
}
