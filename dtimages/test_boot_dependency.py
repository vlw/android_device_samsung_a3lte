#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the real Samsung/device Make rules with a fake boot packer.

Changing the kernel must repack boot even if the DT bytes/mtime do not change.
This tests GNU Make prerequisites, not Kati's implicit DT output handling.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--boot-mk", type=Path, default=Path(__file__).with_name("bootimg.mk"))
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="a3-boot-dependency-") as directory:
    work = Path(directory)
    for name in ("kernel", "ramdisk", "dt.img"):
        (work / name).write_bytes(name.encode())
    packer = work / "packer.py"
    packer.write_text('''#!/usr/bin/env python3
from pathlib import Path
import sys
args=sys.argv[1:]
Path(args[args.index('--output')+1]).write_bytes(Path(args[args.index('--kernel')+1]).read_bytes())
''')
    packer.chmod(0o755)
    makefile = work / "Makefile"
    makefile.write_text(f'''
.DEFAULT_GOAL := all
PRODUCT_OUT := {work}
INSTALLED_BOOTIMAGE_TARGET := {work}/boot.img
INSTALLED_KERNEL_TARGET := {work}/kernel
INSTALLED_DTIMAGE_TARGET := {work}/dt.img
INTERNAL_BOOTIMAGE_FILES := {work}/ramdisk
BOOTIMAGE_EXTRA_DEPS := {work}/dt.img
MKBOOTIMG := {packer}
bootimage-to-kernel = $(INSTALLED_KERNEL_TARGET)
include {args.boot_mk.resolve()}
.PHONY: all
all: $(INSTALLED_BOOTIMAGE_TARGET)
''')
    def build():
        subprocess.run([os.environ.get("MAKE", "make"), "-s", "-f", str(makefile)],
                       cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    build()
    before = (work / "boot.img").read_bytes()
    dt_timestamp = (work / "dt.img").stat().st_mtime_ns
    (work / "kernel").write_bytes(b"new kernel")
    stamp = (work / "boot.img").stat().st_mtime_ns + 2_000_000_000
    os.utime(work / "kernel", ns=(stamp, stamp))
    build()
    assert (work / "boot.img").read_bytes().startswith(b"new kernel"), "boot retained the old kernel"
    assert (work / "dt.img").stat().st_mtime_ns == dt_timestamp
    print("PASS: kernel-only change repacks boot with unchanged DT")
