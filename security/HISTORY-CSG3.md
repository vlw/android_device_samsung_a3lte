# Historical FU / CSG3 investigation — not A300F qualification

> Current status (2026-10-22): native firmware only. No candidate CSG3 TAs or
> cmnlib images are packaged, and no firmware_class.path override is installed.
> The opt-in adapter only accepts the qualified CSG3 boot chain and hashes of
> native partition components. Keymaster aliases merely expand keymaste.* to
> keymaster.* under /system/etc/firmware. BOF7 operation using injected CSG3
> cmnlib is no longer a supported product configuration. Historical experiment
> results below do not override this rule. Reference binaries were preserved in
> out/kernel-runtime-diagnostics/2026-10-22/native-firmware-policy/tee-reference/.

# SM-A300FU CSG3 hardware security adapter

This backend speaks the native protocol of Samsung's Android 6
A300FUXXU1CSG3 Keymaster trusted application. Android-facing code is built from
source in this directory; the actual cryptography, authentication secret and
Gatekeeper secure storage remain in Samsung's signed TA.

Status: builds for LOS19/ARM32; mock-transport tests pass. Fresh CSG3 TA loading
on the BOF7 TrustZone succeeds when the matched CSG3 cmnlib is selected from
vendor, with both installed and stock Android 6 QSEE API clients. See the final
qualification section. No credentials enrolled or secure firmware partitions
flashed. End-to-end hardware protection and FBE are **not yet qualified**.

## Integration

The CSG3 TA command dispatcher at code offset `0x44a6` answers command `0x200`
with interface **1.0**, TA **1.9**. The stock Android `keystore.qcom.so` still
exports HAL API 0.3; it cannot expose this functionality by itself.

The source backend implements the old Qualcomm KM1 wire protocol (12-byte
packed parameters with 32-bit offsets), not the later `*_10` serialization of
newer Qualcomm TAs. Its Android interface is HIDL Keymaster 3.0 using AOSP's
existing `libkeymaster3device` KM1 adapter. LOS19 Keystore2's existing
`km_compat` supports this interface. This does not upgrade the TA's enforced
security properties to KM3, KM4.1 or KeyMint.

Gatekeeper exposes HIDL 1.0 directly, preserving the native TA's timeout status
and authentication token bytes. Both services open the same `keymaster` TA,
so their authentication tokens use the same secure-world secret. There is no
userspace token signing key or software credential-verification fallback in
this backend. The generic AOSP KM1 adapter can perform legacy digest handling
outside TEE; its hardware/software authorization lists are preserved.

Enable in an explicitly prepared build with `A3LTE_TEE_SECURITY=true`. The
product then selects hardware Gatekeeper instead of software Gatekeeper and
adds hardware KM3 alongside the existing **software** KM4.1 instance. A readonly
vendor property starts the two device services. Without the build flag, the
current product selection is unchanged; the new services are disabled even if
someone manually builds/installs their diagnostic targets into the output tree.

The vendor image includes the signed CSG3 TA files in `/vendor/firmware`,
independently of this flag; the flag does not convert credentials or userdata.
There is deliberately no automatic migration, TA firmware overlay,
firmware-flashing script, `fileencryption`, `forceencrypt`, or data deletion.

## Firmware qualification

`QseeTransport` allows the tested BOF7 and reference CSG3 boot chains, checks
that firmware_class.path selects `/vendor/firmware/venus`, and verifies all ten
CSG3 hashes before opening any TA. Keymaster is read from `/vendor/firmware`;
cmnlib is checked through the actual priority-directory links. It then checks the
TA version and sets the native KM1 client version. These are conservative
compatibility checks, not a replacement for secure boot authentication.

The connected phone has BOF7 TZ.BF.3.0.R2-00048. The CSG3 reference contains
TZ.BF.3.0.C3-00052, but replacing TZ was unnecessary for successful TA loading.
The initial EINVAL was resolved by selecting the matched CSG3 cmnlib, rather
than the BOF7 partition copy. No foreign Motorola/QRD TA, TrustZone or bootloader
is used. An allowlisted bootloader property does not authenticate the running
secure-world image, nor do successful load tests qualify every key operation.

Stock source archive: `SAMFW.COM_SM-A300FU_XEF_A300FUXXU1CSG3_fac.zip`.
Reference components extracted from its BL/NON-HLOS and AP/system images are
retained outside source control in `out/keystore-diagnostics/2026-10-20/`.
The stock Gatekeeper and On7 Pro Gatekeeper are byte-identical, SHA256
`fd8b1ba48e512e22d399d309d93adf2d5683cfd2fbdbedc76bf25a2367fcaf20`.
The latter is a reference only, not a runtime dependency.

## Protocol coverage

| Commands | Function |
| --- | --- |
| `0x101`–`0x106` | Query supported algorithms, modes, padding, digests and formats |
| `0x108`, `0x109` | Generate keys and read hardware/software characteristics |
| `0x10b`, `0x10c` | Import keys, export public keys as X.509 |
| `0x10d`, `0x10e` | Explicit deletion of one/all keys, never run at startup |
| `0x10f`, `0x111`–`0x113` | Begin, update, finish, abort |
| `0x200`, `0x207` | Read TA version and set client protocol version |
| `0x1001`–`0x1004` | Gatekeeper enroll, verify, delete one/all users |

CSG3's algorithm table contains RSA, EC, AES and HMAC. Its AES mode table
contains ECB, CBC, CTR and GCM. Tables alone do not prove every operation works;
real symmetric/auth-bound key tests remain necessary.

EC import uses the curve degree, not the private scalar's byte length; it pads
coordinates/scalars to the curve width, including P-521. EC signature conversion
handles ASN.1 sign padding and validates lengths. Requests and replies check
bounds, multiplication/offset overflow and malformed nested parameter offsets.
Update consumes at most 16 KiB per call and returns the actual TA consumption.
Finish places parameters at the declared offset (the QRD donor's old branch
had an inconsistent parameter destination). Failed update/finish operations are
aborted and operation tracking is cleared; destruction releases remaining ops.
Temporary shared buffers and response/import/password buffers are wiped.

`0x107` (entropy injection) is an empty branch in the CSG3 dispatcher. This
backend returns `KM_ERROR_UNIMPLEMENTED` rather than claiming injection
succeeded. The TA has its own RNG; its quality has not been measured here.
Hardware attestation explicitly returns UNIMPLEMENTED; the adapter does not
issue substitute software attestation certificates. Secure key import/wrapped
keys, rollback-resistant key
storage and newer KeyMint-only features are not implemented by this port.

## Validation

Build only these targets (does not access a phone):

```sh
m vendor.a3tee.keymaster@3.0-service vendor.a3tee.gatekeeper@1.0-service \
  a3tee_protocol_test a3tee_protocol_test_device
out/host/linux-x86/nativetest64/a3tee_protocol_test/a3tee_protocol_test
device/samsung/a3lte/security/tests/run-host-tests.sh
```

The standalone host script instruments the backend with ASan/UBSan and uses a
fake in-memory transport. The Soong tests use the same test corpus. Tests cover packed
parameters, malformed response corpus, failures without software fallback,
keyblob characteristics, operation cleanup, finish authentication parameters,
EC DER signatures, P-256/P-521 imports/public export, and Gatekeeper timeouts and
token preservation. The ARM32 test binary also uses only the fake transport.
Building/running it does not exercise or qualify QSEE.

Before enabling on a phone: qualify the matched TZ/TA/cmnlib bundle, both
services' QSEE sessions and SELinux permissions, hardware-enforced
AES-GCM/HMAC/auth-bound keys, token interoperability, persistent PIN retry
throttling and key persistence. Existing software Gatekeeper handles and keys
must not be assumed portable to the hardware backend. Do not test enrollment,
reset, deletion or migration on the current user's credentials without a
separately prepared recovery/migration procedure.

FBE additionally needs kernel fscrypt support and verified Root of Trust / boot
state binding. This port never sends arbitrary root-of-trust (`0x201`) or
secure-storage provisioning commands from Android. It does not establish that
an unlocked legacy boot chain can protect DE keys against replacement of the OS.

All implementation changes are in device trees. System sources were read and
linked, not modified.

## Historical initial qualification on BOF7 (2026-10-20)

The explicit `a3tee_qsee_probe` target only resolves QSEE start/shutdown functions;
it has no TA send-command code. `--inventory` uses driver version and app-lookup
ioctls; `--attach-only` requires an existing canonical app and attaches/detaches
without executing TA commands or registering a HAL.

Live results: QSEOS 0x14, QSEE 0x800000, `keymaster` app ID 3. Stock CSG3
libQSEEComAPI links and attaches to that app successfully under LOS19. Existing
software Keymaster, software Gatekeeper and keystore2 services kept their PIDs.

Before the lookup guard was added, isolated-filename load attempts with both
CSG3 and the exact installed BOF7 image failed with EINVAL. Secure-world returns
differed (-1 vs -38); these are not proof of a single cause or of CSG3 TEE
compatibility/incompatibility. Both signed images contain the internal name
`keymaster`. The SMCv2 load ABI does not pass the caller's filename alias, and
the kernel explicitly pins this canonical app against normal unload. A renamed
file therefore cannot be relied on to provide an independent TA instance.
The probe now refuses any fresh load when canonical `keymaster` already exists.
No identity/version claim for the already-loaded TA is inferred from its app ID.

No TA commands were sent in these probes: in CSG3 even GET_VERSION traverses
RPMB open/create initialization. Root-of-trust/provisioning, enrollment, deletion
and key-generation tests were not performed. A clean, explicitly prepared TEE
boot configuration is still required to qualify the CSG3 backend. Restarting
Android services alone does not reset the pinned secure-world app.

## Vendor firmware packaging

The ten files come from this device's Android 6.0.1 A300FUXXU1CSG3
NON-HLOS image, without modifying signed contents. The FAT basename
`keymaste` is installed as `keymaster`: libQSEEComAPI formats the requested
name directly into `.mdt`/`.bNN` paths on ext4. The transport loads canonical
`keymaster` from `/vendor/firmware`; both services have read-only access to
these `firmware_file` labels. No partition contents are replaced.

`cmnlib` is different: qseecom loads it via kernel `request_firmware`, not
via the directory passed to QSEECom_start_app. This kernel searches
`/firmware/image` before `/vendor/firmware`, so the existing priority directory
`/vendor/firmware/venus` now contains five relative links to vendor cmnlib.
The post-fs device action sets that override before class-core QSEE clients
start. Legacy /system/etc/firmware cmnlib links also point to vendor on a3lte.
Other devices retain their original partition links. The kernel search order
and Venus firmware files are unchanged. The adapter requires the override and
checks the common-library bytes through those links before sending commands.
Changing files cannot replace a common library already resident in TEE: boot
with the correct files and override from the beginning. Hardware services
remain opt-in; successful loading does not qualify credentials or FBE.

## Historical vendor-path qualification follow-up (2026-10-20)

Vendor files and product copy targets were verified byte-for-byte against all
ten CSG3 hashes. Both services and the SELinux policy build successfully;
neverallow/file-context checks pass. Eleven ASan/UBSan backend tests pass.

A new read-only inventory found no canonical keymaster before the test. The
probe's explicit --load-candidate mode checked the five signed CSG3 Keymaster
segments in temporary staging, using the correct keymaster.* basename and
stock CSG3 libQSEEComAPI. It sent no TA RPCs. Fresh start_app still failed with
EINVAL: SCM 0x32000101 returned -2 / secure 0xffffffff. This failure therefore
cannot be explained solely by collision with a pre-existing canonical app.
After the attempt, lookup reported keymaster ID 3 and attach/detach succeeded.
Registration/attachment after a failed load is not proof that TA initialization,
crypto or secure storage work; no version RPC was attempted. The exact secure
failure remains unresolved (including the old TZ/common-library environment).

The running BOF7 partition cmnlib differs from CSG3 in all five hashes. No
vendor files, partition contents, SELinux enforcement, HAL selection or Android
credentials were changed on the phone. Temporary probe/API/firmware files were
removed. No reboot or flash was requested or performed by this task.
The secure-world registration resulting from the attempted load cannot be
claimed undone by deleting the temporary files; the kernel pins keymaster.
Hardware security remains opt-in and BOF7-blocked.

Evidence in out/keystore-diagnostics/2026-10-20/: vendor-firmware-build.log,
vendor-firmware-copy-build.log, vendor-firmware-asan.log,
vendor-firmware-live-inventory.log, vendor-firmware-canonical-load.log,
vendor-firmware-candidate-attach.log, vendor-firmware-kernel-after.log.

## Resolved EINVAL: matched vendor common library (2026-10-20)

The user authorized rebooting for this follow-up. CSG3 cmnlib and Keymaster
were installed in /vendor/firmware, with the five priority-directory links.
Two controlled clean boots then tested fresh canonical keymaster loading:

| Common library | QSEE API client | Fresh CSG3 TA load |
| --- | --- | --- |
| BOF7 partition copy (earlier test) | stock CSG3 Android 6 | EINVAL, SCM -2 / secure -1 |
| CSG3 vendor copy | installed client | success; start/shutdown both 0 |
| CSG3 vendor copy | stock CSG3 Android 6 | success; start/shutdown both 0 |

Each successful test first confirmed canonical keymaster was absent; kernel
then reported `App with id 3 (keymaster) now loaded`. Firmware-class debug
confirmed loading CSG3 cmnlib segments (including b02 size 144236), through
the verified priority links. The client-library control rules out changing
libQSEEComAPI as the explanation for the fix. The old TA/new common-library
combination was not tested. No patch to TZ or kernel was needed.

EINVAL was the driver's wrapper for failure of the secure APP_START call;
the failing input environment was the mixed CSG3 TA / BOF7 common library.
This does not identify the specific internal secure-library symbol or call
that failed. Do not work around the loader error by ignoring SCM failures.

Both source services, the firmware/link install targets and SELinux policy
build successfully, including neverallow and file-context checks. The sysfs
override has a dedicated label: vendor_init can set it, and the two HALs can
read it. No production hardware HAL was installed/started, no TA RPC/version/
crypto/Gatekeeper commands were sent, and no credentials or userdata changed.
Temporary diagnostics were removed. Vendor firmware and links remain installed;
TrustZone/bootloader/modem partitions remain unchanged. Successful TA loading
is established; end-to-end key enforcement and credential migration remain
separate qualification work.

Evidence: `out/keystore-diagnostics/2026-10-20/cmnlib-vendor-load.log`,
`cmnlib-vendor-kernel.log`, `cmnlib-vendor-api6-load.log`,
`cmnlib-api6-kernel.log`, `cmnlib-services-build.log`,
`cmnlib-policy-final.log`. Private strace logs confirm the clients read the
vendor keymaster files. Default software HAL selection remains unchanged.
