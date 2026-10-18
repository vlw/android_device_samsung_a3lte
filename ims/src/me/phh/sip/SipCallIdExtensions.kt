// SPDX-License-Identifier: GPL-2.0
package me.phh.sip

internal fun SipHeadersMap.callIdOrNull(): String? = this["call-id"]?.getOrNull(0)

internal fun SipHeadersMap.callIdOrEmpty(): String = callIdOrNull().orEmpty()

internal fun SipHandler.Call.callIdOrNull(): String? = callHeaders.callIdOrNull()

internal fun SipHandler.Call.callIdOrEmpty(): String = callHeaders.callIdOrEmpty()

internal fun SipRequest.callIdOrEmpty(): String = headers.callIdOrEmpty()

internal fun SipResponse.callIdOrEmpty(): String = headers.callIdOrEmpty()
