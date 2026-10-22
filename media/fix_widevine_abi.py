#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bind a legacy Widevine CDM to the already packaged compatible VNDK protobuf.

Any CDM build that links the unversioned libprotobuf-cpp-lite.so gets that
dependency renamed to the v29 copy; a CDM that already links v29 is left alone.
This fixes loader ABI only; it does not change DRM security levels/provisioning.
"""
from pathlib import Path
import subprocess
import sys

LEGACY = "libprotobuf-cpp-lite.so"
COMPATIBLE = "libprotobuf-cpp-lite-v29.so"

path = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[4]
tool = root / "prebuilts/extract-tools/linux-x86/bin/patchelf-0_9"


def needed():
    result = subprocess.run([str(tool), "--print-needed", str(path)], check=True,
                            stdout=subprocess.PIPE, universal_newlines=True)
    return result.stdout.split()


before = needed()
if COMPATIBLE in before and LEGACY not in before:
    raise SystemExit(0)
if LEGACY not in before:
    print("Widevine CDM does not link %s; nothing to rebind" % LEGACY)
    raise SystemExit(0)
subprocess.run([str(tool), "--replace-needed", LEGACY, COMPATIBLE, str(path)], check=True)
after = needed()
if LEGACY in after or COMPATIBLE not in after:
    raise SystemExit("Widevine CDM protobuf rebind did not apply; inspect %s" % path)
print("Widevine CDM rebound to %s" % COMPATIBLE)
