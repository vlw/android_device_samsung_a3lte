// SPDX-License-Identifier: GPL-2.0
import me.phh.sip.SipAccessNetworkInfo;

public final class SipAccessNetworkInfoTest {
    private static void check(String expected, String operator, int tac, int ci, boolean tdd) {
        String value = SipAccessNetworkInfo.lte(operator, tac, ci, tdd);
        if (expected == null ? value != null : !expected.equals(value))
            throw new AssertionError("unexpected PANI: " + value);
    }
    public static void main(String[] args) {
        check("3GPP-E-UTRAN-FDD;utran-cell-id-3gpp=2570100d60000001", "25701", 214, 1, false);
        check("3GPP-E-UTRAN-TDD;utran-cell-id-3gpp=310260fffffffffff", "310260", 65535, 0xfffffff, true);
        check(null, "25701", Integer.MAX_VALUE, 1, false);
        check(null, "25701", 214, Integer.MAX_VALUE, false);
        check(null, "25701", -1, 1, false);
        check(null, null, 214, 1, false);
        check(null, "2570X", 214, 1, false);
        System.out.println("PASS LTE PANI formatting, PLMN width, duplex and unavailable identities");
    }
}
