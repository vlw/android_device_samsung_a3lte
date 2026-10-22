//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.content.pm.PackageManager
import android.os.SystemClock
import android.provider.ContactsContract
import android.telecom.TelecomManager
import android.telephony.Rlog
import kotlin.concurrent.thread

/**
 * Starts the processes Telecom needs for an incoming call while the SIP dialog
 * is still being set up. On this low-RAM device ActivityManager reaps both the
 * contacts provider (caller lookup during call filtering) and the default
 * dialer (InCallService) as empty processes after 30 minutes, and Telecom then
 * cold-starts them one after the other. Acquiring a provider client launches
 * the hosting process (including all of its providers) without any visible
 * side effect in the app; both launches run in parallel off the SIP thread.
 */
internal object SipIncomingCallPrewarm {
    private const val TAG = "PHH SipPrewarm"

    fun start(ctxt: Context) {
        warmAsync(ctxt, "contacts") { ContactsContract.AUTHORITY }
        warmAsync(ctxt, "dialer") { defaultDialerProviderAuthority(it) }
    }

    private fun warmAsync(ctxt: Context, label: String, authority: (Context) -> String?) {
        thread(name = "SipPrewarm-$label") {
            val startMs = SystemClock.elapsedRealtime()
            try {
                val name = authority(ctxt)
                if (name == null) {
                    Rlog.d(TAG, "No $label provider to prewarm")
                    return@thread
                }
                ctxt.contentResolver.acquireUnstableContentProviderClient(name)?.close()
                Rlog.d(TAG, "Prewarmed $label provider $name in ${SystemClock.elapsedRealtime() - startMs} ms")
            } catch (t: Throwable) {
                Rlog.w(TAG, "Prewarming $label provider failed", t)
            }
        }
    }

    // Any provider hosted in the dialer's main process brings that process up.
    private fun defaultDialerProviderAuthority(ctxt: Context): String? {
        val pkg = ctxt.getSystemService(TelecomManager::class.java)?.defaultDialerPackage ?: return null
        val info = ctxt.packageManager.getPackageInfo(pkg, PackageManager.GET_PROVIDERS)
        val mainProcess = info.applicationInfo.processName
        return info.providers
            ?.firstOrNull { it.processName == mainProcess }
            ?.authority
            ?.substringBefore(';')
    }
}
