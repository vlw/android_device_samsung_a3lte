# SPDX-License-Identifier: Apache-2.0
"""Host-only regression checks; never connects to ADB or a block device."""
import io
import pathlib
import tempfile
import unittest
import zipfile
from unittest.mock import patch
import a3_ota_from_target_files
import non_ab_ota
import ota_no_recovery
import common
import ota_utils
import ota_metadata_pb2
from recovery_policy import check_target, check_ota


def archive(files):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w") as out:
        for name, contents in files.items():
            out.writestr(name, contents)
    data.seek(0)
    return zipfile.ZipFile(data)


class PolicyTest(unittest.TestCase):
    def test_metadata_sidecars_match_zip(self):
        metadata = ota_metadata_pb2.OtaMetadata()
        metadata.type = ota_metadata_pb2.OtaMetadata.BLOCK
        metadata.precondition.device.append("a3lte")
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "metadata"
            ota_utils.WriteMetadata(metadata, str(output))
            restored = ota_metadata_pb2.OtaMetadata()
            restored.ParseFromString(output.with_suffix(".pb").read_bytes())
            self.assertEqual(restored, metadata)
            data = io.BytesIO()
            with zipfile.ZipFile(data, "w") as package:
                ota_utils.WriteMetadata(metadata, package)
            data.seek(0)
            with zipfile.ZipFile(data) as package:
                self.assertEqual(package.read(ota_utils.METADATA_PROTO_NAME),
                                 output.with_suffix(".pb").read_bytes())
                self.assertEqual(package.read(ota_utils.METADATA_NAME),
                                 output.read_bytes())

    def test_reject_stale_payloads(self):
        for name in ["RECOVERY/RAMDISK/init", "IMAGES/recovery.img",
                     "SYSTEM/vendor/etc/recovery.img", "SYSTEM/vendor/bin/install-recovery.sh",
                     "SYSTEM/vendor/recovery-from-boot.p", "OTA/recovery-two-step.img"]:
            with self.subTest(name=name), archive({name: b"old"}) as target:
                with self.assertRaises(ValueError):
                    check_target(target)
        with archive({"IMAGES/system.map": "/vendor/etc/recovery.img 42-69\n"}) as target:
            with self.assertRaises(ValueError):
                check_target(target)

    def test_upstream_changes_stop_packaging(self):
        with self.assertRaises(common.ExternalError):
            ota_no_recovery._drop_once("changed upstream code", "expected fragment")
        with self.assertRaises(common.ExternalError):
            ota_no_recovery._drop_once("fragment fragment", "fragment")

    def test_direct_writers_reject_two_step(self):
        with patch.object(common.OPTIONS, "two_step", True):
            for writer in [non_ab_ota.WriteFullOTAPackage,
                           non_ab_ota.WriteBlockIncrementalOTAPackage]:
                with self.assertRaises(common.ExternalError):
                    writer(None, None)

    def test_allow_clean_target(self):
        with archive({"IMAGES/boot.img": b"boot", "SYSTEM/build.prop": b"props"}) as target:
            check_target(target)

    def test_reject_unsafe_ota(self):
        for script in ['write_raw_image("x", "/dev/block/bootdevice/by-name/recovery");',
                       'package_extract_file("recovery.img", "/dev/block/mmcblk0p17");']:
            with archive({"META-INF/com/google/android/updater-script": script}) as ota:
                with self.assertRaises(ValueError):
                    check_ota(ota)
        with archive({"META-INF/com/google/android/updater-script":
                      'package_extract_file("boot.img", "/dev/block/bootdevice/by-name/boot");'}) as ota:
            check_ota(ota)

    def test_reject_two_step_before_io(self):
        with patch.object(common.OPTIONS, "two_step", True):
            with self.assertRaises(common.ExternalError):
                non_ab_ota.GenerateNonAbOtaPackage("missing.zip", "never-created.zip")

    def test_runtime_partition_map_excludes_recovery(self):
        raw = "/dev/block/bootdevice/by-name/boot /boot emmc defaults defaults\n"
        raw += "/dev/block/bootdevice/by-name/recovery /recovery emmc defaults defaults\n"
        raw += "/dev/block/bootdevice/by-name/system /system ext4 ro wait,recoveryonly\n"
        info = {"no_recovery": "true", "fstab_version": 2}
        paths = []
        def read(path):
            paths.append(path)
            return raw
        fstab = a3_ota_from_target_files._device_fstab(info, None, read)
        self.assertEqual(paths, ["SYSTEM/vendor/etc/fstab.qcom"])
        self.assertIn("/boot", fstab)
        self.assertIn("/system", fstab)
        self.assertNotIn("/recovery", fstab)


if __name__ == "__main__":
    unittest.main()
