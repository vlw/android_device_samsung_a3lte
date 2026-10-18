/* SPDX-License-Identifier: GPL-2.0 */
// Run with the built A3Ims APK on CLASSPATH. No network/SIM requests or property writes.
import java.lang.reflect.*;
import java.net.InetAddress;
import java.util.*;
public final class LegacyPcscfTest {
    @SuppressWarnings("unchecked")
    static List<InetAddress> read(String iface, Map<String, String> values) throws Exception {
        Class<?> type = Class.forName("me.phh.sip.LegacySamsungPcscf");
        Class<?> function = Class.forName("kotlin.jvm.functions.Function1");
        Object callback = Proxy.newProxyInstance(function.getClassLoader(), new Class[]{function},
                (proxy, method, args) -> values.getOrDefault((String)args[0], ""));
        return (List<InetAddress>)type.getMethod("read", String.class, function)
                .invoke(type.getField("INSTANCE").get(null), iface, callback);
    }
    static void check(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        Map<String, String> props = new HashMap<>();
        props.put("ril.pcscfv4.gsm1.0", "192.0.2.10");
        props.put("ril.pcscfv4.gsm1.1", "192.0.2.10");
        props.put("ril.pcscfv4.gsm1.2", "not-a-numeric-address.invalid");
        props.put("ril.pcscfv6.gsm1.0", "::");
        props.put("ril.pcscfv6.gsm1.1", "2001:db8::10");
        props.put("ril.pcscfv6.gsm1.2", "::1");
        List<InetAddress> result = read("rmnet1", props);
        check(result.size() == 2);
        check(result.get(0).getAddress().length == 16);
        check(result.get(1).getHostAddress().equals("192.0.2.10"));
        for (String iface : new String[]{"wlan0", "ipsec0", "rmnet2", "rmnet999", "rmnetx", "", null})
            check(read(iface, props).isEmpty());
        props.put("ril.pcscfv4.gsm0.0", "0.0.0.0");
        check(read("rmnet0", props).isEmpty());
        System.out.println("PASS P-CSCF interface isolation, IPv4/IPv6, deduplication and invalid addresses");
    }
}
