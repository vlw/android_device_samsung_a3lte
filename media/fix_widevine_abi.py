#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bind the pinned angler CDM to the already packaged compatible VNDK protobuf.

This fixes loader ABI only; it does not change DRM security levels/provisioning.
"""
import hashlib
from pathlib import Path
import subprocess
import sys

path = Path(sys.argv[1]).resolve()
original = "66ba66d047044f92eb0eada1faf6a5799ded90ab"
patched = "2f70d96729bc35389d98ab3a1e8f48f4f7b659c2"
digest = hashlib.sha1(path.read_bytes()).hexdigest()
if digest == patched:
    raise SystemExit(0)
if digest != original:
    raise SystemExit("Unqualified Widevine CDM; refusing ABI patch")
root = Path(__file__).resolve().parents[4]
tool = root / "prebuilts/extract-tools/linux-x86/bin/patchelf-0_9"
subprocess.run([str(tool), "--replace-needed", "libprotobuf-cpp-lite.so",
                "libprotobuf-cpp-lite-v29.so", str(path)], check=True)
if hashlib.sha1(path.read_bytes()).hexdigest() != patched:
    raise SystemExit("Unexpected patched CDM hash; inspect output before packaging")
