//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.Rlog
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

object BoundedCloser {
    fun close(
        logTag: String,
        label: String,
        timeoutMs: Long = 1_000L,
        close: () -> Unit,
    ) {
        val finished = AtomicBoolean(false)
        var failure: Throwable? = null
        val closeThread = thread(name = "PhhImsClose-$label", isDaemon = true) {
            try {
                close()
            } catch (t: Throwable) {
                failure = t
            } finally {
                finished.set(true)
            }
        }

        closeThread.join(timeoutMs)
        if (!finished.get()) {
            Rlog.w(logTag, "close $label still running after ${timeoutMs}ms; continuing IMS reconnect")
            return
        }

        failure?.let { Rlog.d(logTag, "close $label failed", it) }
    }
}
