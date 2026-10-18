#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Native protocol/parser tests plus differential checks against both OEM libraries."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
fixture = json.loads((root / "tests/stock_profiles.json").read_text())
assert (
    hashlib.sha256((root / "Tfa9895.cnt").read_bytes()).hexdigest()
    == fixture["provenance"]["container_sha256"]
)
with tempfile.TemporaryDirectory(prefix="a3-tfa-test-") as output:
    binary = Path(output) / "dsp_test"
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(root),
            str(root / "tests/dsp_test.c"),
            str(root / "tfa_container.c"),
            str(root / "tfa_dsp.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    test = subprocess.run(
        [str(binary), str(root / "Tfa9895.cnt")],
        check=True,
        capture_output=True,
        text=True,
    )
    expected = {(c["profile"], c["step"]): c["sha256"] for c in fixture["cases"]}
    seen = set()
    raw = {}
    automatic = {}
    for line in test.stdout.splitlines():
        if line.startswith("auto "):
            _, profile, step, trace = line.split()
            key = int(profile), int(step)
            assert key not in automatic
            automatic[key] = bytes.fromhex(trace)
            continue
        profile, step, trace = line.split()
        key = int(profile), int(step)
        assert key not in seen, key
        seen.add(key)
        actual = hashlib.sha256(bytes.fromhex(trace)).hexdigest()
        raw[key] = bytes.fromhex(trace)
        assert actual == expected[key], f"OEM trace mismatch for profile/step {key}"
    assert seen == expected.keys()
    assert automatic.keys() == expected.keys()
    for key, trace in raw.items():
        adapted = bytearray(trace)
        offset = 0
        gain_records = 0
        while offset < len(adapted):
            if adapted[offset] == 255:
                adapted[offset + 1] = 2 if key[0] == 0 else 0
                offset += 2
                gain_records += 1
            else:
                size = adapted[offset + 2] | adapted[offset + 3] << 8
                offset += 4 + size
        assert offset == len(adapted) and gain_records == 1
        assert automatic[key] == adapted, f"Android gain adaptation mismatch {key}"
    print(test.stderr.strip())
    print(f"{len(seen)}/{len(expected)} profile/volume traces match both OEM libraries")
    print(
        f"{len(automatic)} Android-volume traces retain OEM tuning without double attenuation"
    )
