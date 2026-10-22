# Native A3 Keymaster HAL

Only the device's native `/firmware/image/keymaste.*` and `cmnlib.*` are used.
Nothing writes firmware, TrustZone, bootloader, modem, RPMB provisioning, or
userdata encryption settings.

## Backend selection

No bootloader string, firmware build or file hash is checked. At start the
service tries, in order:

1. The native Keymaster 0.3 module (`/vendor/lib/keystore.qcom.so`). It must
   report a hardware (not software-only) KM0 API, then pass a self-test: a
   fixed public test key (`SelfTestKey.inc`) is imported into a temporary
   blob, signed raw in the TA and verified with its public part. Generation is
   used only if the TA rejects import.
2. A Keymaster 1 TA over QSEE. Any TA answering the KM1 version query is
   probed; it must list AES/HMAC/RSA/EC and a temporary HMAC key must sign,
   verify and reject a damaged MAC. The reported TA version is only logged.
3. The software Keymaster 3 (`PureSoftKeymasterContext`), so keystore keeps
   working on any variant or firmware without a usable TA. Software key blobs
   stay readable; TEE-bound RSA blobs cannot be used while no TA works.

The selected backend is logged as `a3tee-keymaster: Using ...`.

## What is implemented

On the SM-A300F test device (bootloader A300FXXS1BRH1) the Keymaster TA is
byte-identical to the old FU/BOF7 KM0 TA, not the CSG3 KM1 TA. Its Android API is legacy Keymaster 0.3. Sending KM1/Gatekeeper
commands to this TA is not supported. The new HIDL 3.0 service wraps native KM0
RSA through AOSP's Keymaster0Engine and an adapted RSA factory. Keystore2's
existing km_compat supplies the Android12 KeyMint-facing interface.

The native module lives at `/vendor/lib/keystore.qcom.so`. Its old public-key
export does not work with the current crypto ABI. NativeLegacy replaces that
callback with a bounded parser for the public RSA fields in the 1604-byte blob,
using current BoringSSL APIs. Private operations still execute in the native TA.
Only qualified RSA-2048/exponent 65537 is currently enabled. Digesting, padding,
authorization policy, OS/patch metadata and application binding are outside TEE.
Origin is reported UNKNOWN; KM0 cannot prove import versus generation. OS/patch
metadata is explicitly kept out of hardware-enforced authorizations.

AES/HMAC/EC use the existing software implementation. Software key blobs remain
readable, including keys created before enabling this HAL. The software Gatekeeper
and its credential SIDs remain selected. No PIN/password migration is performed.
Hardware attestation, rollback resistance, hardware symmetric keys, secure user
authentication and FBE qualification are not provided by this port. A newer Android
interface does not upgrade the secure application's protocol or capabilities.

On the A300F test device the KM1 probe is never reached: the native KM0 module
passes first. Historical FU experiments in HISTORY-CSG3.md describe the KM1 TA.

## Building

The product always selects this HIDL3 service instead of the software KM4.1
product service; it decides at runtime whether hardware is used. Do not advertise both:
Android12 otherwise includes the software KM4.1 default in shared-secret
negotiation and retries its unimplemented HMAC-sharing call indefinitely.
Software algorithm support remains available through the existing compatibility
library. Gatekeeper stays software unless separately explicitly selected with
`A3LTE_TEE_GATEKEEPER=true`; that mode is not qualified for A300F.

```
m vendor.a3tee.keymaster@3.0-service selinux_policy
m a3tee_legacy_test a3tee_protocol_test a3tee_context_test
```

Variants whose TA fails the self-test keep a software keystore instead of a
broken one. Firmware aliases in Android.mk point only to the native partition.
No signed firmware files are bundled or overridden by this feature.

## Sources

The four files under legacy/keymaster/legacy_support are adapted from AOSP
android11-release system/keymaster (Keymaster0Engine and RSA KM0 factory).
The legacy HAL ABI header is copied from the tree's VNDK30 ARM headers.
Apache notices are retained. Changes adapt the factory signatures to Android12,
restrict native RSA sizes to the qualified configuration and preserve current
software blob parsing. No system source files are modified.

- https://android.googlesource.com/platform/system/keymaster/+/refs/heads/android11-release/legacy_support/keymaster0_engine.cpp
- https://android.googlesource.com/platform/system/keymaster/+/refs/heads/android11-release/legacy_support/rsa_keymaster0_key.cpp

## Qualification

`a3tee_legacy_test` executes real temporary RSA operations in the native TEE,
then exercises the HIDL3 adapter with SHA256 signing, public verification,
application binding and damaged-blob rejection. It does not enroll users or call
delete-all. Run tests sequentially: the upstream KM0 engine has one instance per
process. The service uses one RPC thread. Hardware security cannot be inferred
from the host-only fake-transport tests.

`tests/KeystoreSmoke.java` creates only its own diagnostic aliases and checks
RSA/AES through AndroidKeyStore. Use create/verify/cleanup before the switch and
create-native/verify-native/cleanup-native afterward. It never accesses or deletes
an unrelated alias. Native AES must not claim hardware backing.

Runtime evidence is stored outside source control under
out/keystore-diagnostics/2026-10-22-a300f/. Live results and remaining limits are
recorded in QUALIFICATION-A300F.md. No device reboot or Download Mode is needed
for the service-level tests; SELinux policy compilation alone does not qualify an
enforcing runtime. No encryption conversion or data wipe is part of testing.
