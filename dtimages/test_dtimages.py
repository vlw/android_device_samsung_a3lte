#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host regression tests for QCDT validation and archive-only OTA packaging."""
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


builder = load("a3_dtimages", HERE / "build_dtimages.py")


def fdt(board=1, model=b"test\0"):
    strings = b"qcom,msm-id\0qcom,board-id\0model\0"
    def prop(name, data):
        return struct.pack(">III", 3, len(data), strings.index(name)) + data + b"\0" * (-len(data) % 4)
    tree = struct.pack(">I", 1) + b"\0" * 4
    tree += prop(b"qcom,msm-id", struct.pack(">II", 206, 0))
    tree += prop(b"qcom,board-id", struct.pack(">II", 0xCE08FF01, board))
    tree += prop(b"model", model)
    tree += struct.pack(">II", 2, 9)
    off_tree = 56
    off_strings = off_tree + len(tree)
    return (struct.pack(">10I", 0xD00DFEED, off_strings + len(strings), off_tree,
                        off_strings, 40, 17, 16, 0, len(strings), len(tree)) +
            b"\0" * 16 + tree + strings)


def qcdt(blob):
    key, = builder.dtb_entries(blob)
    size = (len(blob) + 2047) & ~2047
    return (struct.pack("<4sII", b"QCDT", 2, 1) +
            struct.pack("<6I", *key, 2048, size)).ljust(2048, b"\0") + blob.ljust(size, b"\0")


class TableTests(unittest.TestCase):
    def test_qcdt_payload_and_key(self):
        blob = fdt()
        builder.validate_qcdt(qcdt(blob), builder.dtb_entries(blob))

    def test_truncated_and_corrupt_fdt(self):
        for blob in (b"", fdt()[:-1], b"BAD!" + fdt()[4:]):
            with self.assertRaises(ValueError):
                builder.dtb_entries(blob)

    def test_reject_skipped_or_wrong_tree(self):
        blob = fdt()
        for image in (qcdt(blob)[:11], qcdt(blob)[:-1], qcdt(fdt(board=2)),
                      qcdt(fdt(model=b"wrong\0"))):
            with self.assertRaises(ValueError):
                builder.validate_qcdt(image, builder.dtb_entries(blob))
        expected = builder.dtb_entries(blob)
        expected.update(builder.dtb_entries(fdt(board=2)))
        with self.assertRaises(ValueError):
            builder.validate_qcdt(qcdt(blob), expected)

    def test_failed_variant_does_not_publish_any_image(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            name = "msm8916-sec-a3-test"
            (root / (name + ".dtb")).write_bytes(fdt())
            output = root / "out"
            args = types.SimpleNamespace(output_dir=str(output), dtb_dir=str(root),
                                         dtbtool="unused", dtc_dir=str(root), page_size=2048,
                                         f=[name], fu=[name, name], h=[name])
            def tool(cmd, **kwargs):
                Path(cmd[cmd.index("-o") + 1]).write_bytes(qcdt(fdt()))
            with patch.object(builder.subprocess, "run", side_effect=tool):
                with self.assertRaisesRegex(ValueError, "ambiguous"):
                    builder.build(args)
            self.assertEqual(list(output.iterdir()), [])

    def test_unchanged_output_preserves_timestamp(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "image"
            builder.write_changed(path, b"same")
            before = path.stat().st_mtime_ns
            builder.write_changed(path, b"same")
            self.assertEqual(before, path.stat().st_mtime_ns)


class OtaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        common = types.ModuleType("common")
        common.ExternalError = RuntimeError
        common.ZipWriteStr = lambda archive, name, data: archive.writestr(name, data)
        with patch.dict(sys.modules, {"common": common}):
            cls.ota = load("a3_releasetools", HERE.parent / "releasetools/releasetools.py")

    def archive(self, missing=None, mismatch=False, corrupt=False):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            for name, count in (("dtF.img", 8), ("dtFU.img", 4), ("dtH.img", 20)):
                if name == missing:
                    continue
                data = struct.pack("<4sII", b"BAD!" if corrupt else b"QCDT", 2, count) + name.encode()
                archive.writestr("RADIO/" + name, data)
                if name == "dtF.img":
                    archive.writestr("BOOT/dt", b"mismatch" if mismatch else data)
            for name in ("kernel_make.sh", "kernel_unpack.sh", "mkbootimg", "unpackbootimg"):
                if name != missing:
                    archive.writestr("RADIO/" + name, name.encode())
        stream.seek(0)
        return zipfile.ZipFile(stream)

    def context(self, source, target=None):
        script = types.SimpleNamespace(lines=[])
        script.AppendExtra = script.lines.append
        script.Mount = lambda name: None
        script.Unmount = lambda name: None
        return types.SimpleNamespace(input_zip=source, target_zip=target,
                                     output_zip=zipfile.ZipFile(io.BytesIO(), "w"), script=script)

    def test_full_ota_archive_only(self):
        with self.archive() as source:
            context = self.context(source)
            with context.output_zip:
                self.ota.FullOTA_InstallEnd(context)
                self.assertEqual(context.output_zip.read("install/dtimage/dtFU.img"),
                                 source.read("RADIO/dtFU.img"))
                self.assertEqual(len(context.output_zip.namelist()), 6)
                self.assertTrue(any('A300FUXX' in line for line in context.script.lines))
                self.assertTrue(any('A300HXX' in line for line in context.script.lines))

    def test_incremental_uses_target_not_source(self):
        with self.archive(missing="dtFU.img") as old, self.archive() as target:
            context = self.context(old, target)
            with context.output_zip:
                self.ota.IncrementalOTA_InstallEnd(context)
                self.assertEqual(context.output_zip.read("install/dtimage/dtH.img"),
                                 target.read("RADIO/dtH.img"))

    def test_reject_missing_stale_or_corrupt_archive(self):
        for options in ({"missing": "dtFU.img"}, {"missing": "mkbootimg"},
                        {"mismatch": True}, {"corrupt": True}):
            with self.archive(**options) as source:
                context = self.context(source)
                with context.output_zip, self.assertRaises(RuntimeError):
                    self.ota.FullOTA_InstallEnd(context)
                self.assertEqual(context.script.lines, [])


if __name__ == "__main__":
    unittest.main()
