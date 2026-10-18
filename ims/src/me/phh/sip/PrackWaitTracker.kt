//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.telephony.Rlog

class PrackWaitTracker {
    private val lock = Object()
    private val pending = mutableSetOf<Int>()

    fun add(id: Int) {
        synchronized(lock) {
            pending += id
        }
    }

    fun ack(id: Int) {
        synchronized(lock) {
            pending -= id
            lock.notifyAll()
        }
    }

    fun waitFor(id: Int) {
        synchronized(lock) {
            while (pending.contains(id)) {
                lock.wait(1000)
            }
        }
    }

    fun clearAndNotifyAll() {
        synchronized(lock) {
            pending.clear()
            lock.notifyAll()
        }
    }

    fun dropStaleBeforeAccept(logTag: String) {
        synchronized(lock) {
            if (pending.isNotEmpty()) {
                Rlog.w(logTag, "Dropping stale PRACK waits before accept: $pending")
                pending.clear()
                lock.notifyAll()
            }
        }
    }
}
