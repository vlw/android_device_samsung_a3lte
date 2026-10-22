# A300F BRH1 qualification — 2026-10-22

Device: SM-A300F, A300FXXS1BRH1, LOS19/Android12 eng. Native firmware inventory
and the complete test logs are in out/keystore-diagnostics/2026-10-22-a300f/.
Keymaster and cmnlib hashes are pinned in NativeFirmwareHashes.inc. The Keymaster
b02 hash is db37715aa93e9cc5e82c70e990fbb3a22f2c96a6f9427a8e13f1a7d4ae4ff05d,
identical to old BOF7 code. It does not implement the CSG3 KM1 protocol.

## Passed on the connected phone

- Native KM0 module open and RSA-2048 generation through native keymaste.
- Fixed public-key export, raw private RSA operation and independent public verification.
- HIDL3 SHA256/RSA signing, public-key verification, wrong application binding and
  damaged blob rejection. Both live test cases pass.
- Registration of android.hardware.keymaster@3.0::IKeymasterDevice/default under
  hal_keymaster_default, UID system, groups system/drmrpc.
- AndroidKeyStore generation and use of RSA: securityLevel=1,
  insideSecureHardware=true. AES round trip: insideSecureHardware=false
  (the KM3 compatibility path reports the software policy at KEYSTORE level 100).
- A pre-existing diagnostic software RSA and AES key, generated before installing
  the new HAL, remained usable with securityLevel=0 after switching.
- Both new and old diagnostic keys worked after restarting the HAL and keystore2.
- Keystore shared-secret negotiation now completes. Software KM4.1's manifest
  and init service are omitted in the selected hardware product configuration.
- Eleven ASan/UBSan fake-transport tests; seven native-firmware policy tests
  (one build-environment test skipped); service build, SELinux/neverallow and
  file-context checks pass.

The running native service remains active. Its init rc, HIDL3 manifest, binary,
libkeymaster3device and the enabling vendor property are installed in system.
The obsolete software KM4.1 rc/manifest were backed up before removal. Software
Gatekeeper remains selected; no real credential enrollment was done.

## Preservation and limits

Only system files and disposable diagnostic keys/files were changed. No Download
Mode, reboot, firmware/TrustZone/bootloader/modem writes, encryption conversion,
credential migration or data wipe occurred. The key database was backed up before
switching; its private backup is kept outside Git. Test aliases were removed afterward.
Native TA/common-library selection has no firmware_class override.

Persistence was checked across Android process restarts, not a power cycle. The
running system is SELinux permissive; policy compilation passes but a fully
enforcing boot is not qualified. Boot itself was not modified or reboot-tested.
A clean boot is still needed to validate automatic init startup.

Only native RSA-2048/exponent 65537 generation/signing is qualified. Key import,
other sizes, hardware symmetric algorithms, hardware Gatekeeper, auth-bound keys,
FBE and full encryption security are not qualified. The Android API version is
3.0; secure-world functionality remains KM0 with software authorization policy.
Keep A3LTE_TEE_SECURITY=true in builds intended to retain access to newly created
hardware keys. Reverting to software-only HAL cannot use those opaque RSA blobs.

The AOSP KM0 engine and factory were adapted in device/, not system/. The prior
CSG3 backend remains separate and fails its firmware guard on this F/BRH1 target.

## Android6 HAL comparison and runtime health follow-up

The unchanged CSG3 Android6 keystore.qcom.so was staged as a userspace-only
diagnostic. SHA256: 5d73748d831959e4f3e64783e60fd7d882886affc4e7b0d606b39408866f6356.
It exports KM0 module API 0.3 and flags 0x2 (standalone hardware blobs). Against
native F firmware, RSA-2048 PKCS8 import and private operation/public verification
pass. Public export uses the same current-crypto callback as the new wrapper.
This does not test the binary's original incompatible public export callback.
No donor TA, cmnlib or Gatekeeper was loaded.

The stock Android6 gatekeeper.msm8916.so is a QSEE client, not software password
verification. It expects the newer keymaster TA's commands 0x1001–0x1004.
The native F executable handler at 0x324b dispatches only commands 1–4:
RSA generate/import/sign/verify. Running its exact unsupported-command branch
offline confirms rejection of KM1 queries/operations and GK commands without
writing a response. tests/check-native-dispatch.py reproduces this using Unicorn
and refuses any different code hash. No enrollment/deletion commands were sent
to the live TA. Replacing the Android6 HAL alone cannot add these TA handlers.

Health audit found an integration side effect of restarting hwservicemanager:
LiveDisplay belongs to late_start and was not restarted with main/hal/early_hal.
Its old process survived but lost HIDL registration, blocking system_server in
IAdaptiveBacklight.getService and causing repeated framework watchdog restarts.
Restarting vendor.livedisplay-hal-2-0-samsung-qcom restored registration and
stopped this cycle. A phone/kernel reboot was not used. Native Keymaster and
keystore2 retained their PIDs during these watchdog incidents; the old software
KM4.1 aborts at 21:34 belonged to removing its manifest, and that service is stopped.

Do not treat a successful Keystore smoke test as whole-framework health proof.
If hwservicemanager must be restarted during a live integration, also re-register
late_start HALs such as LiveDisplay and verify system_server stability afterward.
Collected evidence: health-full.txt, watchdog-anr.txt and stock6-compare/ under
the diagnostic output directory. No new watchdog ANRs appeared after recovery
during the follow-up observation interval.
