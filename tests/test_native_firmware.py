#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regressions for A3 native signed-firmware selection, not hardware load tests."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
DEVICE = ROOT / "device/samsung/a3lte"
COMMON = ROOT / "device/samsung/msm8916-common"
parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("--copy-files", type=Path)
parser.add_argument("--packages", type=Path)
args, rest = parser.parse_known_args()


def run_rules(block, variables):
    with tempfile.TemporaryDirectory(prefix="a3-native-firmware-") as tmp:
        base = Path(tmp)
        rules = base / "Makefile"
        rules.write_text(".DEFAULT_GOAL := all\nTARGET_OUT_ETC := " + str(base / "etc") +
                         "\n" + block + "\nall: $(ALL_DEFAULT_INSTALLED_MODULES)\n")
        subprocess.run(["make", "--no-print-directory", "-s", "-f", str(rules),
                        *variables], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return {p.name: str(p.readlink()) for p in (base / "etc/firmware").glob("*")}


class NativeFirmwareTest(unittest.TestCase):
    def test_no_boot_global_override(self):
        for tree in [DEVICE, COMMON]:
            for pattern in ["*.rc", "*.sh", "*.mk"]:
                for p in tree.rglob(pattern):
                    if "tests" in p.parts:
                        continue
                    for line in p.read_text(errors="replace").splitlines():
                        if line.lstrip().startswith("#"):
                            continue
                        self.assertNotRegex(line, r"(?:write|echo|printf).*firmware_class.*/path", str(p))
                        self.assertNotRegex(line, r"firmware_class\.path=", str(p))
        self.assertFalse((DEVICE / "media/init.a3lte.venus.rc").exists())

    def test_make_rejects_signed_image_copies(self):
        text = (DEVICE / "Android.mk").read_text()
        block = text.split("# A3_NATIVE_FIRMWARE_GUARD_BEGIN", 1)[1].split(
            "# A3_NATIVE_FIRMWARE_GUARD_END", 1)[0]
        with tempfile.TemporaryDirectory(prefix="a3-firmware-guard-") as tmp:
            makefile = Path(tmp) / "Makefile"
            for destination, reject in [
                ("system/vendor/firmware/venus/venus.mdt", True),
                ("system/vendor/firmware/cmnlib.b02", True),
                ("system/vendor/firmware/keymaster.mdt", True),
                ("system/vendor/firmware/modem.b00", True),
                ("system/vendor/firmware/wcnss.mdt", True),
                ("system/vendor/firmware/cpp_firmware_v1_4_0.fw", False),
                ("system/vendor/firmware/a300_pm4.fw", False),
                ("system/vendor/firmware/nfc/sec_s3fwrn5_firmware.bin", False),
            ]:
                makefile.write_text("PRODUCT_COPY_FILES := source:" + destination +
                                    "\n" + block + "\nall:;@true\n")
                result = subprocess.run(["make", "-s", "-f", str(makefile)],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode != 0, reject, destination)
                if reject:
                    self.assertIn(b"native partition firmware", result.stderr)

    def test_real_cmn_make_rules(self):
        text = (COMMON / "Android.mk").read_text()
        block = text[text.index("CMN_IMAGES :="):text.index("ISDB_IMAGES :=")]
        for device in ["a3lte", "other-common-device"]:
            links = run_rules(block, ["TARGET_DEVICE=" + device])
            self.assertEqual(len(links), 5)
            self.assertTrue(all(target == "/firmware/image/" + name for name, target in links.items()))

    def test_optional_keymaster_aliases_are_native(self):
        text = (DEVICE / "Android.mk").read_text()
        if "A3LTE_KM_NAMES" not in text:
            self.skipTest("The optional TEE adapter is not present in this checkout")
        start = text.index("ifeq ($(A3LTE_TEE_SECURITY),true)")
        block = text[start:text.index("\nendif", start) + len("\nendif")]
        self.assertEqual(run_rules(block, ["A3LTE_TEE_SECURITY=false"]), {})
        links = run_rules(block, ["A3LTE_TEE_SECURITY=true"])
        self.assertEqual(len(links), 5)
        for name, target in links.items():
            self.assertEqual(target, "/firmware/image/" + name.replace("keymaster", "keymaste"))
        transport = (DEVICE / "security/QseeTransport.cpp").read_text()
        self.assertNotIn('"/vendor/firmware', transport)
        self.assertNotIn("A300FUXXU1BOF7", transport)

    def test_common_vendor_respects_native_policy(self):
        with tempfile.TemporaryDirectory(prefix="a3-common-vendor-") as tmp:
            base = Path(tmp)
            sets = {}
            for flag in ["true", "false"]:
                makefile = base / "Makefile"
                copies = base / "copies"
                makefile.write_text("DEVICE_USES_NATIVE_VENUS_FIRMWARE := " + flag +
                    "\ninclude " + str(ROOT / "vendor/samsung/msm8916-common/msm8916-vendor.mk") +
                    "\n$(file >" + str(copies) + ",$(PRODUCT_COPY_FILES))\nall:;@true\n")
                subprocess.run(["make", "-s", "-f", str(makefile)], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                sets[flag] = set(copies.read_text().split())
            removed = sets["false"] - sets["true"]
            self.assertEqual(len(removed), 7)
            self.assertTrue(all("/firmware/venus." in entry for entry in removed))
            self.assertFalse(sets["true"] - sets["false"])

    def test_fu_import_removed(self):
        vendor = ROOT / "vendor/samsung/a3lte"
        self.assertFalse(list((vendor / "proprietary/vendor/firmware/venus").glob("venus.*")))
        extraction = (DEVICE / "proprietary-files.txt").read_text()
        self.assertNotRegex(extraction, r"(?m)^[^#\n]*firmware/(?:venus|cmnlib|keymaster)[./]")

    def test_resolved_product_files(self):
        if not args.copy_files:
            self.skipTest("Pass --copy-files from soong_ui --dumpvar-mode PRODUCT_COPY_FILES")
        for entry in args.copy_files.read_text().split():
            self.assertNotRegex(entry, r"/firmware/(?:venus[./]|cmnlib\.|keymaster\.)")
            self.assertNotIn("init.a3lte.venus.rc", entry)
        if args.packages:
            self.assertFalse(any("a3tee." in p for p in args.packages.read_text().split()))


if __name__ == "__main__":
    unittest.main(argv=[__file__, *rest])
