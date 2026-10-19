// SPDX-License-Identifier: GPL-2.0
package me.phh.ims

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.Handler
import android.os.HandlerThread
import android.telephony.Rlog
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import me.phh.sip.SipHandler

/** Observes the real radio handover; never synthesizes SRVCC from a RAT change. */
class LegacySamsungSrvcc(private val manager: TelephonyManager, private val sip: SipHandler) :
    TelephonyCallback(), TelephonyCallback.SrvccStateListener {
    private val thread = HandlerThread("A3ImsSrvcc").apply { start() }
    private val handler = Handler(thread.looper)
    private val encoder = SamsungSrvccCallList()
    private var previous: ByteArray? = null
    private var lastSend = 0L
    private var closed = false
    private val transfer = SrvccTransferState()
    private var transferDialogs = emptySet<String>()
    private val timeout = Runnable {
        transfer.timeout()
        sip.onSrvccStateChanged(TelephonyManager.SRVCC_STATE_HANDOVER_FAILED, timedOut = true)
        Rlog.w("A3ImsSrvcc", "SRVCC start timed out; retaining IMS session")
    }
    private val poll = object : Runnable {
        override fun run() {
            if (closed) return
            var hasCalls = false
            try {
                // Freeze the modem table during transfer. Clearing it early can
                // make the vendor RIL discard its SRVCC completion indication.
                if (!transfer.isPending) {
                    val calls = sip.srvccCallSnapshot()
                    hasCalls = calls.isNotEmpty()
                    val packet = encoder.encode(calls)
                    val now = android.os.SystemClock.elapsedRealtime()
                    if (previous?.contentEquals(packet) != true || (hasCalls && now - lastSend >= 5_000L)) {
                        val result = exchange(packet)
                        if (result == 0) { previous = packet; lastSend = now }
                    }
                }
            } catch (e: Exception) {
                Rlog.w("A3ImsSrvcc", "Call-list synchronization failed: ${e.javaClass.simpleName}")
            }
            handler.postDelayed(this, if (hasCalls) 250L else 1_000L)
        }
    }
    private val stateChanged: () -> Unit = { requestSync() }
    fun requestSync() {
        handler.post {
            if (!closed) {
                handler.removeCallbacks(poll)
                handler.post(poll)
            }
        }
    }
    init {
        sip.srvccCallsChanged = stateChanged
        manager.registerTelephonyCallback(java.util.concurrent.Executor { handler.post(it) }, this)
        Rlog.i("A3ImsSrvcc", "Call-list observer ready")
        handler.post(poll)
    }
    private fun exchange(packet: ByteArray): Int = try {
        LocalSocket().use {
            it.connect(LocalSocketAddress("a3ims-radio", LocalSocketAddress.Namespace.ABSTRACT))
            it.soTimeout = 1_000
            it.outputStream.write(packet)
            it.inputStream.read()
        }
    } catch (_: Exception) { -1 }

    override fun onSrvccStateChanged(state: Int) {
        if (closed) return
        Rlog.i("A3ImsSrvcc", "Radio SRVCC state=$state")
        val calls = sip.srvccCallSnapshot()
        val hasCalls = if (state == TelephonyManager.SRVCC_STATE_HANDOVER_COMPLETED)
            calls.any { it.key in transferDialogs } else calls.isNotEmpty()
        when (transfer.update(state, hasCalls)) {
            SrvccTransferState.Action.START -> {
                transferDialogs = calls.map { it.key }.toSet()
                handler.postDelayed(timeout, 10_000L)
            }
            SrvccTransferState.Action.COMPLETE, SrvccTransferState.Action.RETAIN -> {
                handler.removeCallbacks(timeout)
                previous = null
            }
            else -> return
        }
        sip.onSrvccStateChanged(state)
    }
    fun close() {
        if (sip.srvccCallsChanged === stateChanged) sip.srvccCallsChanged = null
        manager.unregisterTelephonyCallback(this)
        handler.post {
            closed = true
            handler.removeCallbacksAndMessages(null)
            // Feature retirement must clear a stale PS table; this does not hang
            // up any CS call. The CS call list belongs to the original RIL.
            exchange(encoder.encode(emptyList()))
            thread.quitSafely()
        }
    }
}
