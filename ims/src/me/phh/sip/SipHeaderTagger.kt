//SPDX-License-Identifier: GPL-2.0
package me.phh.sip

object SipHeaderTagger {
    fun addTag(header: String, tag: String): String {
        val trimmedHeader = header.trim()
        if (trimmedHeader.contains(";tag=", ignoreCase = true)) return trimmedHeader
        if (trimmedHeader.contains(">")) return "$trimmedHeader;tag=$tag"
        if (trimmedHeader.startsWith("sip:", ignoreCase = true) ||
            trimmedHeader.startsWith("sips:", ignoreCase = true) ||
            trimmedHeader.startsWith("tel:", ignoreCase = true)
        ) {
            return "<$trimmedHeader>;tag=$tag"
        }
        return "$trimmedHeader;tag=$tag"
    }
}
