# A3 native signed firmware policy

All SM-A300F/FU/H builds use the signed subsystem and TEE images from the
phone's matching firmware partitions. `firmware_class.path` stays unset;
there is no product init action that gives donor images global priority.
The existing kernel lookup order and `/firmware` / `/firmware-modem` mounts
are retained. A missing or rejected native image is a diagnostic failure,
not a reason to silently select a donor build.

## Removed on 2026-10-22

- The imported FU Venus 00155 set, its extraction entries, copy targets and
  init rule. The update had no demonstrated speed or bug-fix benefit.
- The older common vendor Venus 00109 fallback for A3. Common-vendor packaging
  for other products remains unchanged. The A3 product declares
  `DEVICE_USES_NATIVE_VENUS_FIRMWARE` before inheriting common configuration.
- Unconditional CSG3 cmnlib/Keymaster packaging from the unfinished TEE work,
  cmnlib links through the Venus directory and vendor-directed legacy links.
  `/system/etc/firmware/cmnlib.*` points to `/firmware/image/cmnlib.*` again.
- The BOF7 + injected CSG3 cmnlib product configuration. Experimental TEE
  source is retained outside the default product selection; its current
  qualification requires native CSG3 images and the CSG3 boot chain. It must
  not manufacture compatibility by changing the global firmware search path.

Ten untracked TEE reference binaries were preserved outside vendor sources,
with hashes, under
`out/kernel-runtime-diagnostics/2026-10-22/native-firmware-policy/tee-reference/`.
The six imported Venus files remain recoverable from vendor commit c7e4372.
No modem, APNHLOS, TrustZone, bootloader or recovery partition was written.

The optional Keymaster prototype's read-only aliases expand Samsung's FAT
`keymaste.*` basename to the API's `keymaster.*`. They point to the native
partition and are generated only for an explicitly enabled adapter; they do
not copy TA contents or change the common-library selection. The default
product continues using its existing software security services.

## Audit of the remaining paths

| Component | Source / evidence | Result |
|---|---|---|
| Venus | Native `/firmware/image`; F runtime reports VIDEO.VE.1.8-00130 | No vendor image or priority override |
| cmnlib / Keymaster | Native partition links; candidate TA files not packaged | No cross-build TEE injection |
| Modem / MBA | Existing links to `/firmware-modem/image` | No donor modem installation or forced image |
| WCNSS | Existing native partition links | No signed DSP override. The separate WLAN NV stock-model difference is recorded below |
| NFC sec_s3fwrn5 | Vendor file SHA256 equals the SM-A300F BRH1 stock system image, 119084 bytes | Retained; the old A320 provenance label did not imply different bytes |
| GPU a300_pfp / a300_pm4 | Both vendor files equal SM-A300F BRH1 stock byte-for-byte | Retained; these are stock system firmware assets |
| Camera CPP 1.1.1 / 1.1.6 / 1.4.0 | All equal SM-A300F BRH1 stock byte-for-byte | Retained |
| Camera CPP 1.2.0 | Matches FU CSG3 stock; differs from F stock; inherited 2020 asset | Retained as existing HAL hardware-version asset; current F logs show active CPP 1.4.0.4, not 1.2.0. This is not runtime qualification of every H/FU hardware revision |
| AV1 / VP9 | Source-built CPU codec implementations; prior frame/pixel/seek tests and measured improvement are in NATIVE_DECODERS.md | Retained; no signed firmware replacement |
| Camera / TFA / IMS userspace libraries | Explicitly requested userspace functionality, separate from PIL firmware selection | Not removed wholesale as part of signed-firmware cleanup |
| GNSS | The stock HAL bridge is a diagnostic target under tests, not a product HAL; aiding and Samsung QMI helpers do not install a modem image | No donor GNSS subsystem image selected |
| swcodec linker-config / gps_debug bind mounts | Existing Android ABI/config integration, not signed firmware selection | Retained; not evidence of firmware version substitution |

This audit checks the current product graph and load paths. It does not claim
runtime hardware qualification of models that were not connected or prove
that every userspace blob is optimal.

## Separate inherited WLAN NV finding

The common `WCNSS_qcom_wlan_nv.bin` is byte-identical to FU CSG3 stock,
but differs from F BRH1 stock in 53 of 29816 bytes. It entered the common
tree in 45d608d7a (2025-11-01, switching CAF data to SEC data), before the
Venus update. The installed phone's NV hash matches the common source file.
`WCNSS_cfg.dat` equals F stock; `WCNSS_qcom_cfg.ini` has several differences
(scan period, HT40, PNO and coexistence options).

These are host-side radio/configuration data, not the signed WCNSS DSP image
and not a firmware_class path override. The 53-byte difference is not yet
proof of a functional defect or proof that one model's values can safely
replace another's. Field semantics and model-specific selection need separate
analysis. This cleanup did not replace the NV/INI or restart Wi-Fi, and does
not claim stock-equivalent WLAN calibration across F/FU/H. Exact hashes and
differences are recorded in `wlan-stock-comparison.json` beside the audit logs.

## Regression protection

The A3 Android.mk rejects PRODUCT_COPY_FILES containing signed Venus, TEE,
modem/MBA, WCNSS/Pronto or ADSP images. GPU/camera/NFC system assets are not
mistaken for those partition images. This check runs during normal build
makefile processing, including when a future extraction regenerates vendor
makefiles.

The regression script also runs the real cmnlib Make rules in a temporary
directory, checks optional native aliases when present, checks production init
scripts and checks the resolved product copy/package lists:

```
source build/envsetup.sh
lunch lineage_a3lte-eng
build/soong/soong_ui.bash --dumpvar-mode PRODUCT_COPY_FILES > /tmp/a3-copy-files
build/soong/soong_ui.bash --dumpvar-mode PRODUCT_PACKAGES > /tmp/a3-packages
python3 device/samsung/a3lte/tests/test_native_firmware.py \
  --copy-files /tmp/a3-copy-files --packages /tmp/a3-packages
```

## Live validation after removal

On the connected A300F the global path was cleared with a terminating NUL
(a zero-byte write does not update the module string). Reading the parameter
returns zero bytes; the sysfs inode's nominal size is not its content length.
The 29 obsolete init/image/link artifacts were archived, removed, and native
cmnlib links restored. The system filesystem was remounted read-only.

Without a reboot or media-service restart, native Venus loaded 00130, AVC
Surface decoding returned 60/60 frames, the hardware encoder returned 30
frames and independent FFmpeg decoding validated all 30. Boot ID remained
`ee3241c7-e26d-43c7-aebc-22c14c5964f5`, tainted=0. Source graph generation,
QSEE transport compilation, native symlink targets and seven policy regression
tests passed. Physical FU/H verification was not performed.

Full evidence, firmware hashes and pre-cleanup backups:
`out/kernel-runtime-diagnostics/2026-10-22/native-firmware-policy/`.
Previously built OTA archives predate this cleanup; a newly built ROM will use
these rules. The connected phone has already been cleaned directly.
