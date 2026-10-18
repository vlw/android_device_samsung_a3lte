// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.os.SystemClock
import android.telephony.Rlog
import java.util.concurrent.ConcurrentHashMap

internal class RecentCallIdCache(
    private val tag: String,
    private val label: String,
    private val ttlMs: Long,
) {
    private val entries = ConcurrentHashMap<String, Long>()

    fun remember(callId: String, reason: String) {
        if (callId.isBlank()) return

        entries[callId] = SystemClock.elapsedRealtime()
        Rlog.d(tag, "Remembering $label Call-ID: callId=$callId reason=$reason")
    }

    fun contains(callId: String): Boolean {
        if (callId.isBlank()) return false

        val now = SystemClock.elapsedRealtime()
        val then = entries[callId] ?: return false
        if (now - then > ttlMs) {
            entries.remove(callId)
            return false
        }

        return true
    }

    fun clear() {
        entries.clear()
    }
}
