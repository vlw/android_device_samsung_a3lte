# Preserve separately installed TWRP

Applies to a3lte eng, userdebug and user ROMs. Recovery is maintained separately;
this ROM must neither build a Lineage recovery image nor replace TWRP.
The old `WITH_TWRP` setting is not an exception to this policy. Build standalone
TWRP with its separate recovery tree/product.

## Confirmed overwrite path

On the connected eng device, `/vendor/etc/recovery.img` (12,859,408 bytes),
`/vendor/bin/install-recovery.sh` and `vendor_flash_recovery.rc` were present.
The script checks and flashes the **recovery** partition with that image.
The init service starts when `persist.vendor.recovery_update=true`. The current
value was false. The Updater exposes that preference when the script exists.
Nothing about this path requires userdebug; a persisted preference can explain
why only some installations encounter it. No claim about the frequency of
reported incidents is made.

A300FU/H DT adaptation scripts were also audited: they read/write `by-name/boot`,
not recovery. On the device, boot resolves to mmcblk0p16 and recovery to mmcblk0p17.
The normal Android init scripts do not contain another recovery flash command.
This does not cover user-installed root modules or arbitrary external flashers.

## Build protection

* `TARGET_NO_RECOVERY=true` and `PRODUCT_BUILD_RECOVERY_IMAGE=false` disable the
  image in all variants. Board validation rejects explicitly enabling the image
  or embedding recovery resources in boot/vendor_boot.
* The device-owned ETC module overrides inherited recovery modules and vendor
  `applypatch`, removing the automatic flash service from the normal product's
  dependency closure. OTA's updater has its own patch implementation.
* A late vendor init override makes `vendor_flash_recovery` a disabled no-op even
  if an old service definition is present. The default update preference is false.
* CleanSpec removes old generated recovery artifacts and forces system/vendor
  image regeneration. Target-files/OTA validation rejects recovery payloads,
  installers and stale recovery entries in image block maps.

## Non-A/B OTA packaging

AOSP's non-A/B generator rejects `no_recovery=true`, requires a recovery patch,
includes recovery.img in full ZIPs, and implements two-step recovery flashing.
Disabling the image alone therefore either breaks ZIP generation or leaves an
unsafe workaround. No framework/build-system source is changed here.

`Android.mk` selects `a3_ota_from_target_files` and supplies the fstab metadata
needed to retain the normal OTA build target. The entry point reuses the existing
`releasetools_ota_from_target_files` library, including its non-A/B generator,
block updates, metadata and signing. There is no generator fork in this tree.

`ota_no_recovery.py` excludes four exact recovery-only fragments from three
upstream functions **in memory**: the no-recovery rejection, recovery-patch
requirement, and full/incremental recovery-image loading. System source files
are never edited. If any fragment changes or occurs more than once, packaging
stops for review. Two-step OTA is rejected before generation and by both writers,
so its remaining upstream branches cannot run. Target inputs and the output ZIP
are validated before finalization/signing; no fake recovery image is substituted.

The runtime `SYSTEM/vendor/etc/fstab.qcom` supplies the partition map without a
recovery ramdisk; `/recovery` is removed from the host generator's map. For full
OTA addon.d backup/restore, host prebuilt rules read backuptool directly from
`vendor/lineage/prebuilt/common/bin`, installing resources under the host output's
`etc/a3-ota/`. The scripts are not duplicated in device sources or installed in
Android. Build the wrapper target to also install its required host resources;
keep that host directory when moving the tool. Python 3 is embedded.

## Verification

Build explicit tools after lunch:

```
m a3_ota_from_target_files a3_recovery_policy_test a3lte_no_recovery_update
out/host/linux-x86/bin/a3_recovery_policy_test
```

Host tests cover forbidden payloads and stale block maps, safe/unsafe scripts,
two-step rejection before file access (including direct writers), rejection of
changed upstream fragments, and the runtime fstab without /recovery.
Full and incremental ZIPs were additionally generated from synthetic ext4
fixtures with addon.d backup enabled and the real a3lte DT adaptation extension.
Their scripts and block data, plus the full ZIP's backup tools, were checked; neither ZIP contains a
recovery payload or recovery-writing operation. The adapter produced byte-identical
block/boot payloads and updater scripts to the previous fork on both fixtures;
full ZIP signing with the standard test key also passed. These are host-only packaging
fixtures, **not flashable ROMs**. A complete fresh ROM build and installation
were not performed as part of this policy change.

Build-variable checks for eng/userdebug/user confirm no recovery image target.
Diagnostics are under `out/recovery-diagnostics/2026-10-19/`. Phone inspection was
read-only: no reboot, service restart, partition flash or file removal was done.
Existing installer files on an already installed older ROM remain until it is
updated; these source changes apply to newly built ROMs.
