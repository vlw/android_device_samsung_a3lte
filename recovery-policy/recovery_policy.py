# SPDX-License-Identifier: Apache-2.0
"""Reject recovery-bearing target files rather than silently repackaging them."""
import re


def forbidden(name):
    name = name.strip("/")
    leaf = name.rsplit("/", 1)[-1]
    return (name.startswith(("RECOVERY/", "BOOT/RECOVERY/")) or
            leaf in {"recovery.img", "recovery-two-step.img", "ramdisk-recovery.img",
                     "recovery-from-boot.p", "recovery-resource.dat", "install-recovery.sh"})


def check_target(target):
    bad = [name for name in target.namelist() if forbidden(name)]
    # A stale block map can hide deleted files in an already built system image.
    for name in target.namelist():
        if name.startswith("IMAGES/") and name.endswith(".map"):
            for line in target.read(name).decode("utf-8").splitlines():
                if line and forbidden(line.split()[0]):
                    bad.append(name + ": " + line.split()[0])
    if bad:
        raise ValueError("a3lte: recovery payloads are forbidden: " + ", ".join(bad[:10]))


def check_ota(package):
    bad = [name for name in package.namelist() if forbidden(name)]
    script = package.read("META-INF/com/google/android/updater-script").decode("utf-8")
    if bad or re.search(r"recovery(?:\.img|-from-boot|/|\")|by-name/recovery|mmcblk0p17", script):
        raise ValueError("a3lte: OTA must not contain or write recovery")
