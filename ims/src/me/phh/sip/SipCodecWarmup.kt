//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.os.SystemClock
import android.telephony.Rlog
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * Pays the one-time codec costs after IMS registration instead of on the first
 * INVITE: the MediaCodec list build behind the AMR-WB probe and the stock EVS
 * library load/self-test. Both results are cached lazies; an INVITE that races
 * with the warm-up simply waits for the same initialization, so the thread
 * keeps normal priority to avoid inverting behind a waiting SIP thread.
 */
internal object SipCodecWarmup {
    private const val TAG = "PHH SipCodecWarmup"
    private val started = AtomicBoolean(false)

    fun start() {
        if (!started.compareAndSet(false, true)) return
        thread(name = "SipCodecWarmup") {
            val startMs = SystemClock.elapsedRealtime()
            val amrWb = SipAudioCodecNegotiator.amrWbAvailable
            val amrWbMs = SystemClock.elapsedRealtime() - startMs
            val evs = EvsSwbProfile.available()
            Rlog.d(TAG, "Codecs ready: amrWb=$amrWb in $amrWbMs ms, evs=$evs " +
                "in ${SystemClock.elapsedRealtime() - startMs - amrWbMs} ms")
        }
    }
}
