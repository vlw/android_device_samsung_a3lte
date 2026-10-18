// SPDX-License-Identifier: GPL-2.0
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.List;
import me.phh.sip.SipRegistrationExpiry;

public final class SipRegistrationExpiryTest {
    private static void check(long want, String contact, String expires) {
        Map<String, List<String>> headers = new HashMap<>();
        if (contact != null) headers.put("Contact", Arrays.asList(contact));
        if (expires != null) headers.put("Expires", Arrays.asList(expires));
        long actual = SipRegistrationExpiry.refreshDelayMillis(headers);
        if (actual != want) throw new AssertionError("wanted " + want + ", got " + actual);
    }
    public static void main(String[] args) {
        check(1_200_000, "<sip:ue@192.0.2.1>;expires=3600", "7200");
        check(96_000, "<sip:ue@192.0.2.1>;expires=120", "3600");
        check(48_000, "<sip:a@192.0.2.1>;expires=120, <sip:b@192.0.2.2>;expires=60", null);
        check(48_000, "<sip:a@192.0.2.1>;Expires = \"60\";q=1", "3600");
        check(96_000, null, "120");
        check(1_000, null, "1");
        check(1_200_000, null, "9223372036854775807");
        check(1_200_000, null, "999999999999999999999999");
        check(1_200_000, "<sip:a@192.0.2.1>;expires=oops", "-1");
        check(1_200_000, null, null);
        try { check(0, "<sip:a@192.0.2.1>;expires=0", "3600");
            throw new AssertionError("unregistered binding accepted");
        } catch (IllegalStateException expected) {}
        System.out.println("PASS REGISTER expiry, Contact precedence, timer bounds and removed binding");
    }
}
