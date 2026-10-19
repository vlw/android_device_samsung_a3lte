# Wi-Fi tethering regression checks

These are optional host/device probes, not product packages. Run from the Android
source root with a root ADB connection. They start the real tethering service,
not only `cmd wifi start-softap` (which does not enable DHCP/NAT tethering).
No call, reset, formatting or network-list deletion is involved.

## Build the framework control probe

After building the framework and Wi-Fi/tethering libraries for the target:

```sh
mkdir -p out/a3-hotspot-probe/classes out/a3-hotspot-probe/dex
javac --release 8 \
  -cp out/soong/.intermediates/frameworks/base/framework-minus-apex/android_common/combined/framework-minus-apex.jar:out/soong/.intermediates/packages/modules/Wifi/framework/framework-wifi/android_common_apex30/combined/framework-wifi.jar:out/soong/.intermediates/packages/modules/Connectivity/Tethering/common/TetheringLib/framework-tethering/android_common_apex30/javac/framework-tethering.jar \
  -d out/a3-hotspot-probe/classes \
  device/samsung/a3lte/tests/hotspot/TetheringProbe.java
out/soong/host/linux-x86/bin/d8 --min-api 31 \
  --output out/a3-hotspot-probe/dex out/a3-hotspot-probe/classes/*.class
adb push out/a3-hotspot-probe/dex/classes.dex /data/local/tmp/a3-tethering-probe.dex
```

Start using a temporary SSID and a strong, temporary WPA2 password:

```sh
adb shell CLASSPATH=/data/local/tmp/a3-tethering-probe.dex \
  app_process /system/bin TetheringProbe start TEST_SSID TEST_PASSWORD
```

The probe refuses to replace an active AP or overwrite an outstanding backup.
It saves the original SoftAP configuration in a root-only file before changing
it. `start` success means the framework accepted the request; also wait for
`AP-ENABLED` and `TetheredState` with `lastError = 0` before connecting a client.
Do not delete the backup before restoring the configuration with `stop`.

## Check the real client path

Use an independent Wi-Fi adapter/client. Preserve its existing connections and
routes. Obtain an address with DHCP, rather than assigning one manually. Record
the offered address, subnet, router, DNS server and lease time. With NetworkManager,
`nmcli -f IP4,DHCP4 device show CLIENT_INTERFACE` exposes the lease options.

Connect the phone to a real upstream. If temporarily enabling mobile data,
record and restore its prior setting. Keep traffic small. Confirm the selected
upstream with `adb shell dumpsys tethering`.

On the client, ensure a route to the chosen public DNS server goes through the
phone's AP gateway, not through another Ethernet connection or VPN. Keep this
route scoped to the test destination; do not replace the host default route.
Then run:

```sh
python3 device/samsung/a3lte/tests/hotspot/client_dns_probe.py CLIENT_DHCP_ADDRESS
```

The probe validates complete UDP and TCP replies from `1.1.1.1:53`. Check the
phone's `/proc/net/nf_conntrack` immediately: the printed client/source ports
must match entries whose reply destination is the phone's upstream address.
Also record `iptables -t nat -nvxL tetherctrl_nat_POSTROUTING` and
`iptables -nvxL tetherctrl_counters` before/after. This distinguishes real NAT
from a client accidentally using another connection. Check DNS relay separately
by querying the DNS server offered in the DHCP lease.

Repeat after stopping/starting the AP and reconnecting the client. Check dmesg
for HDD context ASSERTs, warnings, subsystem restarts and crashes; check Wi-Fi
metrics for hostapd/setup failures. Inspect tethering logs for DHCP, forwarding,
offload and SELinux errors. A successful IPv4 test does not validate IPv6
forwarding, sustained throughput, multiple clients or physical RF coverage.

On this 3.10 kernel the supported forwarding path is netfilter NAT. The device
overlay disables TC BPF tethering acceleration because the accounting backport
does not implement the required clsact/redirect path. Netd's BPF socket/UID
accounting remains enabled. In this Android 12 branch,
`BpfCoordinator.maybeAttachProgram()` does not check the offload flag: it still
tries to attach TC programs and can log `EINVAL`. The overlay disables the
offload forwarding rules; it does not eliminate those unconditional probes or
implement missing kernel TC facilities. An unavailable hardware Offload HAL can
also be reported before the framework selects its normal software fallback.

## Restore

```sh
adb shell CLASSPATH=/data/local/tmp/a3-tethering-probe.dex \
  app_process /system/bin TetheringProbe stop
```

Remove the temporary client connection and route, restore the client's prior
Wi-Fi radio state and the phone's mobile data state, and verify that the phone
reconnects to its original Wi-Fi. Do not reboot solely to stop this test.
