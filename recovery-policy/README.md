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

`Android.mk` selects `a3_ota_from_target_files` for this device and provides the
fstab metadata needed for the normal OTA build target. The entry point uses the
standard signing/metadata/block-update code with `a3_non_ab_ota.py` as backend.
That file is derived from `build/make/tools/releasetools/non_ab_ota.py` at build/make
revision `ec4184bcd771e59d3d108115507587444697be38`, retaining its license.
Changes remove recovery payloads and the two-step implementation, accept only
no-recovery targets, and validate both the target-files and generated ZIP.
Signing and ordinary boot/system updates remain upstream behavior. Two-step OTA
is explicitly rejected. Full and block incremental OTA are supported.

The runtime `SYSTEM/vendor/etc/fstab.qcom` supplies the partition map without a
recovery ramdisk; `/recovery` is removed from the host generator's map. The two
host-bundled `install/bin/backuptool*` scripts are unmodified copies from
`vendor/lineage/prebuilt/common/bin` in this checkout, because AOSP omits INSTALL/
when no recovery is built. They preserve addon.d backup/restore without storing
an installer or a recovery image in Android. Keep them in sync with Lineage's
versions when updating this branch. Host Python 3 is embedded for reproducibility.

## Verification

Build explicit tools after lunch:

```
m a3_ota_from_target_files a3_recovery_policy_test a3lte_no_recovery_update
out/host/linux-x86/bin/a3_recovery_policy_test
```

Host tests cover forbidden payloads and stale block maps, safe/unsafe scripts,
two-step rejection before file access, and the runtime fstab without /recovery.
Full and incremental ZIPs were additionally generated from synthetic ext4
fixtures with addon.d backup enabled and the real a3lte DT adaptation extension.
Their scripts, block data and backup tools were checked; neither ZIP contains a
recovery payload or recovery-writing operation. These are host-only packaging
fixtures, **not flashable ROMs**. A complete fresh ROM build and installation
were not performed as part of this policy change.

Build-variable checks for eng/userdebug/user confirm no recovery image target.
Diagnostics are under `out/recovery-diagnostics/2026-10-19/`. Phone inspection was
read-only: no reboot, service restart, partition flash or file removal was done.
Existing installer files on an already installed older ROM remain until it is
updated; these source changes apply to newly built ROMs.
