#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Package isolated A3 QCDT v2 images; reject skipped/ambiguous DTB entries."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def root_properties(blob):
    """Read root FDT properties without requiring a host Python FDT module."""
    if len(blob) < 40:
        raise ValueError("truncated FDT header")
    magic, total, off_struct, off_strings, _, _, _, _, size_strings, size_struct = struct.unpack_from(">10I", blob)
    if magic != 0xD00DFEED or total != len(blob):
        raise ValueError("invalid FDT magic/size")
    if off_strings + size_strings > total or off_struct + size_struct > total:
        raise ValueError("FDT block outside image")
    strings = blob[off_strings:off_strings + size_strings]
    end = off_struct + size_struct
    pos, depth, props = off_struct, 0, {}
    while pos + 4 <= end:
        token, = struct.unpack_from(">I", blob, pos)
        pos += 4
        if token == 1:  # FDT_BEGIN_NODE
            stop = blob.find(b"\0", pos, end)
            if stop < 0:
                raise ValueError("unterminated FDT node")
            pos = (stop + 4) & ~3
            depth += 1
        elif token == 2:  # FDT_END_NODE
            depth -= 1
        elif token == 3:  # FDT_PROP
            if pos + 8 > end:
                raise ValueError("truncated FDT property")
            length, nameoff = struct.unpack_from(">II", blob, pos)
            pos += 8
            stop = strings.find(b"\0", nameoff)
            if stop < 0 or pos + length > end:
                raise ValueError("invalid FDT property")
            if depth == 1:
                props[strings[nameoff:stop].decode("ascii")] = blob[pos:pos + length]
            pos += (length + 3) & ~3
        elif token == 4:  # FDT_NOP
            pass
        elif token == 9:  # FDT_END
            if depth != 0:
                raise ValueError("unbalanced FDT")
            return props
        else:
            raise ValueError("invalid FDT token")
    raise ValueError("missing FDT_END")


def pairs(value):
    if not value or len(value) % 8:
        raise ValueError("expected nonempty pairs of DT cells")
    return struct.iter_unpack(">II", value)


def dtb_entries(blob):
    props = root_properties(blob)
    # QCDT v2 uses chipset/revision plus board/subtype (no PMIC ID field).
    return {(chip, board, subtype, revision): blob
            for chip, revision in pairs(props["qcom,msm-id"])
            for board, subtype in pairs(props["qcom,board-id"])}


def validate_qcdt(image, expected):
    if len(image) < 12:
        raise ValueError("truncated QCDT")
    magic, version, count = struct.unpack_from("<4sII", image)
    if magic != b"QCDT" or version != 2 or count != len(expected):
        raise ValueError("QCDT header/count mismatch (possibly skipped DTBs)")
    if 12 + count * 24 > len(image):
        raise ValueError("truncated QCDT table")
    seen = set()
    for i in range(count):
        chip, board, subtype, revision, offset, size = struct.unpack_from("<6I", image, 12 + i * 24)
        key = chip, board, subtype, revision
        if key in seen or key not in expected:
            raise ValueError("unexpected/duplicate QCDT key: " + str(key))
        # dtbTool's entry size includes page padding, unlike FDT totalsize.
        blob = expected[key]
        if offset < 12 + count * 24 or offset + size > len(image) or size < len(blob):
            raise ValueError("invalid QCDT payload bounds")
        if image[offset:offset + len(blob)] != blob:
            raise ValueError("QCDT payload mismatch: " + str(key))
        seen.add(key)


def write_changed(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as tmp:
        temp = Path(tmp.name)
        tmp.write(data)
    try:
        temp.chmod(0o644)
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


def build(args):
    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=True)
    result = {}
    with tempfile.TemporaryDirectory(prefix="a3-dtimages-", dir=output) as work:
        for model in ("F", "FU", "H"):
            stage = Path(work) / model
            stage.mkdir()
            expected = {}
            for name in getattr(args, model.lower()):
                if Path(name).name != name or not name.startswith("msm8916-sec-a3"):
                    raise ValueError("invalid DTB name: " + name)
                src = Path(args.dtb_dir) / (name + ".dtb")
                blob = src.read_bytes()
                entries = dtb_entries(blob)
                if expected.keys() & entries.keys():
                    raise ValueError("ambiguous DTBs within model " + model)
                expected.update(entries)
                shutil.copyfile(src, stage / src.name)
            target = Path(work) / ("dt" + model + ".img")
            subprocess.run([str(Path(args.dtbtool).resolve()), "-2", "-s", str(args.page_size),
                            "-p", str(Path(args.dtc_dir).resolve()) + "/",
                            "-o", str(target), str(stage) + "/"], check=True)
            result[model] = target.read_bytes()
            validate_qcdt(result[model], expected)
            print("A3 " + model + ": " + str(len(expected)) + " verified QCDT entries", flush=True)
    # Publish only after every variant has passed validation. dt.img is the
    # default F image used by Samsung mkbootimg; FU/H stay separate for OTA.
    for model, data in result.items():
        write_changed(output / ("dt" + model + ".img"), data)
    write_changed(output / "dt.img", result["F"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dtb-dir", "dtbtool", "dtc-dir", "output-dir"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--page-size", type=int, default=2048)
    for model in ("f", "fu", "h"):
        parser.add_argument("--" + model, nargs="+", required=True)
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, "A3 DT image generation failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
