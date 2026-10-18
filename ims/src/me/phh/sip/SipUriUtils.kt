// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

internal fun extractUriFromNameAddr(header: String): String {
    val trimmed = header.trim()
    val nameAddrUri = Regex("<\\s*([^>]+)\\s*>").find(trimmed)?.groups?.get(1)?.value
    return (nameAddrUri ?: trimmed.substringBefore(";")).trim()
}

internal fun extractCallerNumberFromHeader(header: String): String {
    val uri = extractUriFromNameAddr(header)
    val number = when {
        uri.startsWith("tel:", ignoreCase = true) ->
            uri.substringAfter(":").substringBefore(";")
        uri.startsWith("sip:", ignoreCase = true) || uri.startsWith("sips:", ignoreCase = true) ->
            uri.substringAfter(":").substringBefore("@").substringBefore(";")
        else -> uri.substringBefore(";")
    }.trim().trim('<', '>', '"')

    return number.ifBlank { header }
}

internal fun extractDestinationFromContact(contact: String): String {
    val uri = extractUriFromNameAddr(contact)
    return if (uri.startsWith("sip:", ignoreCase = true) ||
        uri.startsWith("sips:", ignoreCase = true) ||
        uri.startsWith("tel:", ignoreCase = true)) {
        uri
    } else {
        contact
    }
}
