//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipUplinkGain {
    private const val PERSIST_PROPERTY = "persist.sys.phhims.uplink_gain_q8"
    private const val RO_PROPERTY = "ro.phhims.uplink_gain_q8"

    private const val UNSET_Q8 = 0
    private const val UNITY_Q8 = 256

    fun configuredGainQ8(): Int {
        val persistGain = android.os.SystemProperties.getInt(
            PERSIST_PROPERTY,
            UNSET_Q8,
        )

        val rawGain = if (persistGain != UNSET_Q8) {
            persistGain
        } else {
            android.os.SystemProperties.getInt(
                RO_PROPERTY,
                UNITY_Q8,
            )
        }

        // Keep the property safe:
        // 128 = -6.0 dB, 256 = unity, 512 = +6.0 dB, 768 = +9.5 dB.
        return rawGain.coerceIn(128, 768)
    }

    fun propertySummary(): String =
        "persist=$PERSIST_PROPERTY ro=$RO_PROPERTY"

    fun applyInPlace(
        buffer: ByteArray,
        size: Int,
        gainQ8: Int,
    ) {
        if (gainQ8 == UNITY_Q8 || size < 2) {
            return
        }

        var i = 0
        val end = size and -2

        while (i < end) {
            val sample = (buffer[i].toInt() and 0xff) or (buffer[i + 1].toInt() shl 8)
            val boosted = (sample * gainQ8) / UNITY_Q8
            val clipped = boosted.coerceIn(
                Short.MIN_VALUE.toInt(),
                Short.MAX_VALUE.toInt(),
            )

            buffer[i] = (clipped and 0xff).toByte()
            buffer[i + 1] = ((clipped shr 8) and 0xff).toByte()
            i += 2
        }
    }
}
