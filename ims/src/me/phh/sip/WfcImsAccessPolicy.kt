package me.phh.sip

internal object WfcImsAccessPolicy {
    // ImsMmTelManager.WIFI_MODE_WIFI_ONLY
    const val WIFI_MODE_WIFI_ONLY = 0

    // ImsMmTelManager.WIFI_MODE_CELLULAR_PREFERRED
    const val WIFI_MODE_CELLULAR_PREFERRED = 1

    // ImsMmTelManager.WIFI_MODE_WIFI_PREFERRED
    private const val WIFI_MODE_WIFI_PREFERRED = 2

    internal data class Snapshot(
        val wifiOnly: Boolean,
        val wifiPreferredOrOnly: Boolean,
        val iwlanReady: Boolean,
        val airplaneMode: Boolean,
        val convergenceWindow: Boolean,
    ) {
        fun toLogString(): String =
            "wifiOnly=$wifiOnly " +
                "wifiPreferredOrOnly=$wifiPreferredOrOnly " +
                "iwlanReady=$iwlanReady " +
                "airplaneMode=$airplaneMode " +
                "convergenceWindow=$convergenceWindow"
    }

    fun isWifiOnly(enabled: Boolean?, mode: Int?): Boolean =
        enabled == true && mode == WIFI_MODE_WIFI_ONLY

    fun isWifiPreferredOrOnly(enabled: Boolean?, mode: Int?): Boolean {
        val currentMode = mode ?: return false
        return enabled == true && currentMode != WIFI_MODE_CELLULAR_PREFERRED
    }

    private fun isWifiOnlyOrPreferredMode(mode: Int?): Boolean =
        mode == WIFI_MODE_WIFI_ONLY || mode == WIFI_MODE_WIFI_PREFERRED

    fun shouldSkipReconnectForWifiModeOnlyChange(
        oldMode: Int?,
        newMode: Int?,
        imsReady: Boolean,
        registeredOverIwlan: Boolean,
    ): Boolean {
        return isWifiOnlyOrPreferredMode(oldMode) &&
            isWifiOnlyOrPreferredMode(newMode) &&
            imsReady &&
            registeredOverIwlan
    }

    fun isWaitingForRequiredAccessAfterWfcChange(
        convergenceWindow: Boolean,
        wifiOnly: Boolean,
        wifiPreferredOrOnly: Boolean,
        registeredOverLte: Boolean,
        registeredOverIwlan: Boolean,
    ): Boolean {
        if (!convergenceWindow) {
            return false
        }

        val waitingForIwlan = wifiOnly && registeredOverLte

        // Cellular preferred is not cellular-only. If IMS is already registered
        // over IWLAN, allow the call to use VoWiFi instead of rejecting it
        // during the convergence window. Otherwise mobile-preferred WFC can
        // block calls exactly when the modem has no usable cellular IMS path
        // and the framework correctly selected VoWiFi as fallback.
        return waitingForIwlan
    }

    fun shouldPreferIwlanForImsAccessNow(snapshot: Snapshot): Boolean {
        if (snapshot.wifiOnly) {
            return true
        }

        if (snapshot.iwlanReady && snapshot.airplaneMode) {
            return true
        }

        return snapshot.iwlanReady && snapshot.wifiPreferredOrOnly && snapshot.convergenceWindow
    }

    fun shouldWarnAboutCellularImsMismatch(
        snapshot: Snapshot,
        registeredOverLte: Boolean,
    ): Boolean {
        if (!registeredOverLte) {
            return false
        }

        return snapshot.wifiOnly ||
            (snapshot.airplaneMode && snapshot.wifiPreferredOrOnly && snapshot.iwlanReady)
    }
}
