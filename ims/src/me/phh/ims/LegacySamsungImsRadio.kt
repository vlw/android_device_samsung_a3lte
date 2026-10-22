// SPDX-License-Identifier: GPL-2.0
package me.phh.ims

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.Handler
import android.os.HandlerThread
import android.telephony.Rlog

/** Synchronizes the AP SIP registration with the legacy CP's voice-domain state. */
class LegacySamsungImsRadio {
    private val thread = HandlerThread("A3ImsRadio").apply { start() }
    private val handler = Handler(thread.looper)
    private var registered = false
    private var features = 0
    private var closed = false
    private var lastResult = -1
    private var retryDelayMs = MIN_RETRY_MS
    private val sync = object : Runnable {
        override fun run() {
            val result = sendState()
            if (result != lastResult) {
                Rlog.i("A3ImsRadio", "Legacy radio state synchronization result=$result")
                lastResult = result
            }
            // Success keeps the NAS heartbeat. A radio without a loaded SIM
            // can stay unready indefinitely, so back off instead of
            // reconnecting every second; update() still resyncs immediately.
            val delayMs = if (result == 0) HEARTBEAT_MS else retryDelayMs
            retryDelayMs = if (result == 0) MIN_RETRY_MS else (retryDelayMs * 2).coerceAtMost(MAX_RETRY_MS)
            if (!closed) handler.postDelayed(this, delayMs)
        }
    }

    private companion object {
        const val HEARTBEAT_MS = 10_000L
        const val MIN_RETRY_MS = 1_000L
        const val MAX_RETRY_MS = 30_000L
    }

    init { handler.post(sync) }

    fun update(slotId: Int, registered: Boolean, voice: Boolean, sms: Boolean) {
        if (slotId != 0) return // SM-A300FU is single-SIM.
        handler.post {
            if (!closed) {
                this.registered = registered
                features = if (registered) (if (voice) 1 else 0) or (if (sms) 2 else 0) else 0
                retryDelayMs = MIN_RETRY_MS
                handler.removeCallbacks(sync)
                handler.post(sync)
            }
        }
    }

    private fun sendState(): Int = try {
        LocalSocket().use { socket ->
            socket.connect(LocalSocketAddress("a3ims-radio", LocalSocketAddress.Namespace.ABSTRACT))
            socket.soTimeout = 1_000
            socket.outputStream.write(byteArrayOf(65, 51, 73, 77, 1,
                (if (registered) 1 else 0).toByte(), features.toByte(), 0))
            socket.inputStream.read()
        }
    } catch (_: Exception) { -1 }

    fun close() {
        handler.post {
            closed = true
            handler.removeCallbacks(sync)
            registered = false
            features = 0
            sendState()
            thread.quitSafely()
        }
    }
}
