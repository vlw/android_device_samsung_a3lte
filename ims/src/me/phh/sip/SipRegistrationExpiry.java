// SPDX-License-Identifier: GPL-2.0
package me.phh.sip;

import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Conservative refresh timing from REGISTER 200 OK, without wall-clock time. */
public final class SipRegistrationExpiry {
    private static final Pattern CONTACT_EXPIRY = Pattern.compile(
            ";\\s*expires\\s*=\\s*\"?([0-9]+)\"?\\s*(?=[;,]|$)", Pattern.CASE_INSENSITIVE);

    private SipRegistrationExpiry() {}

    public static long refreshDelayMillis(Map<String, List<String>> headers) {
        long contactExpiry = Long.MAX_VALUE;
        long globalExpiry = Long.MAX_VALUE;
        for (Map.Entry<String, List<String>> header : headers.entrySet()) {
            if (header.getKey().equalsIgnoreCase("contact")) {
                for (String contact : header.getValue()) {
                    Matcher matcher = CONTACT_EXPIRY.matcher(contact);
                    while (matcher.find()) {
                        contactExpiry = Math.min(contactExpiry, seconds(matcher.group(1)));
                    }
                }
            } else if (header.getKey().equalsIgnoreCase("expires")) {
                for (String value : header.getValue()) {
                    globalExpiry = Math.min(globalExpiry, seconds(value.trim()));
                }
            }
        }
        // Contact expires overrides the generic Expires field (RFC 3261 10.2.4).
        // With multiple bindings, renewing before the earliest is conservative.
        long expiry = contactExpiry != Long.MAX_VALUE ? contactExpiry : globalExpiry;
        if (expiry == 0) throw new IllegalStateException("REGISTER removed its binding");
        if (expiry == Long.MAX_VALUE) return 1_200_000L;
        // Renew at 80%, and at least every 20 minutes. Cap before multiplication
        // so an untrusted, arbitrarily large expiry cannot overflow the timer.
        return Math.max(1_000L, Math.min(expiry, 1_500L) * 800L);
    }

    private static long seconds(String value) {
        try {
            long seconds = Long.parseLong(value);
            return seconds >= 0 ? seconds : Long.MAX_VALUE;
        } catch (NumberFormatException ignored) {
            return Long.MAX_VALUE;
        }
    }
}
