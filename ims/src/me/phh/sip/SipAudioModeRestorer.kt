//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

import android.content.Context
import android.media.AudioManager
import android.telephony.Rlog

object SipAudioModeRestorer {
    fun restoreAfterImsCall(
        logTag: String,
        context: Context,
        reason: String,
        previousMode: Int? = null
    ) {
        val audioManager = try {
            context.getSystemService(AudioManager::class.java)
        } catch (t: Throwable) {
            Rlog.d(logTag, "Audio mode restore skipped; AudioManager unavailable: $reason", t)
            return
        }

        val currentMode = audioManager.mode
        val wantedMode = when (previousMode ?: currentMode) {
            AudioManager.MODE_IN_CALL,
            AudioManager.MODE_IN_COMMUNICATION,
            AudioManager.MODE_RINGTONE -> AudioManager.MODE_NORMAL
            else -> previousMode ?: currentMode
        }

        if (currentMode == wantedMode) {
            Rlog.d(logTag, "Audio mode restore not needed: reason=$reason currentMode=$currentMode previousMode=$previousMode")
            return
        }

        Rlog.d(
            logTag,
            "Restoring audio mode after IMS call: reason=$reason " +
                "currentMode=$currentMode previousMode=$previousMode wantedMode=$wantedMode",
        )
        try {
            audioManager.clearCommunicationDevice()
        } catch (t: Throwable) {
            Rlog.d(logTag, "clearCommunicationDevice failed during IMS audio restore: $reason", t)
        }
        try {
            audioManager.mode = wantedMode
        } catch (t: Throwable) {
            Rlog.d(logTag, "Setting audio mode failed during IMS audio restore: $reason", t)
        }

    }
}
