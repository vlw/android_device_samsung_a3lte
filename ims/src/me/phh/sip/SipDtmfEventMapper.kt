//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipDtmfEventMapper {
    fun eventForChar(c: Char): Int? =
        when (c.toUpperCase()) {
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' -> c.digitToInt()
            '*' -> 10
            '#' -> 11
            'A' -> 12
            'B' -> 13
            'C' -> 14
            'D' -> 15
            else -> null
        }

    fun durationSteps(durationSamples: Int): List<Int> =
        listOf(
            durationSamples / 4,
            durationSamples / 2,
            durationSamples,
            durationSamples,
            durationSamples,
            durationSamples,
        )
}
