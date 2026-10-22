# Widevine on SM-A300F BRH1 — 2026-10-22

## Outcome

The existing Widevine CDM did not load on Android12. dlopen failed on
`google::protobuf::internal::empty_string_`: the angler OPR6 CDM expects an older
protobuf ABI than Android12's system library. Binding only this CDM to the
already-packaged `libprotobuf-cpp-lite-v29.so` fixes loading. Platform protobuf
is not replaced. MediaDrm now reports scheme supported, vendor Google and L3.

L1 is not established. L3 sessions also need normal provisioning: the probe
returns NotProvisionedException. No provisioning request/response, content
license, keybox installation, or keybox contents were transferred by this task.
Android's getMaxSecurityLevel() constant is not the device's Widevine level;
the legacy HIDL1.0 bridge cannot establish per-session L1 from that API alone.

## Native secure-world capability and blockers

The matching firmware partition includes widevine.mdt and b00–b03. The native
TA loads successfully through QSEE, and its queried OEMCrypto API version is 9.
Two Samsung MSM8916 userspace OEMCrypto clients were tested from temporary
staging, not installed as product dependencies:

- https://api.github.com/repos/therootlord/android_vendor_samsung_msm8916-common/git/blobs/38968989134ec9cb77e4fe424bb6006c4b7e72c1
  SHA256 619d0f8c1236ff433c97af3a13d16f88cb9481028f392443251df58d6f14474b,
  HLOS API10, native TA API9, initialize returns zero.
- https://api.github.com/repos/retiredtab/vendor_samsung_msm8916-common/git/blobs/eb1d9021c47383f3f7a11930bd889a7f83841ce4
  SHA256 f87bad955591a5b18cbe18ea63c9c9cc1aa7b576ad7fa1833273814e1d0efefb,
  HLOS API11, native TA API9, initialize returns zero.

Both return 2147483647 from IsKeyboxValid; the CDM falls back to L3. There is
no /factory/wv.keys provisioning input. This does not distinguish a missing
factory keybox, inaccessible/invalid secure storage or an integration mismatch.
Initialization success does not qualify all OEMCrypto command layouts or L1.
No conclusion that factory credentials are irrecoverably lost is justified.

The BRH1 Android5 and FU CSG3 Android6 system images do not include liboemcrypto.so;
both include legacy L3 Widevine libraries. The native TA nevertheless contains
L1 code paths. That is capability evidence, not proof of factory L1 provisioning.
The phone reports ro.boot.warranty_bit=1; its causal effect on this rejection is
not established. Relocking, fuse changes and firmware changes are not attempted.
AVC secure playback is declared in media_codecs.xml; protected decode/render
and authorized L1 content playback have not been qualified.

OEMCrypto requires a compatible implementation and the complete protected path,
not merely a reported property value:
https://developers.google.com/widevine/access
https://android.googlesource.com/platform/docs/source.android.com/+/idea133-weekly-release/src/devices/drm.jd

## Preservation and evidence

Only the CDM's system/vendor ELF dependency was changed on the phone. Native
firmware, TrustZone and boot were not replaced; there was no reboot. Only the
DRM HAL was restarted. Native QSEE/SFS operations during initialization and
validation access the existing /efs secure-filesystem service (including its
normal counter/housekeeping operations). This is not an EFS provisioning test;
no direct EFS/keybox writes or key extraction were performed by the diagnostics.

No device identifiers or keybox contents are printed by the probes. The optional
native capability probe refuses to run when /factory/wv.keys exists, preventing
the CDM's automatic factory-keybox import path. Temporary donor libraries are
removed after the investigation. Do not package them as qualified L1 support.

Evidence is in out/drm-diagnostics/2026-10-22/: linkage-current.log,
linkage-protobuf-fix.log, fixed-context-probe.log, donors/*-probe.log and source
metadata. Native TA loading and version queries succeeded; keybox validation did
not. The firmware-class override remains empty and Keymaster stays active.

The device fixup script pins the original CDM SHA1 and expected patched SHA1;
unknown binaries are rejected and a second application is a no-op. The original
extraction pin remains the unmodified factory file's hash. Vendor/system sources
outside device trees and vendor blobs are not modified.
