#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Standard OTA entry point with the a3lte recovery-preserving non-A/B backend."""
import sys
import common
import ota_from_target_files
from a3_non_ab_ota import GenerateNonAbOtaPackage

# Non-A/B AOSP normally reads fstab from RECOVERY/RAMDISK. Use the runtime
# device fstab instead, without packaging a recovery ramdisk just for metadata.
_original_fstab = common._FindAndLoadRecoveryFstab


def _device_fstab(info, input_file, read_helper):
    if info.get("no_recovery") == "true":
        fstab = common.LoadRecoveryFSTab(
            read_helper, info["fstab_version"], "SYSTEM/vendor/etc/fstab.qcom",
            info.get("system_root_image") == "true")
        if not fstab:
            raise common.ExternalError("a3lte: missing runtime fstab for OTA")
        fstab.pop("/recovery", None)
        return fstab
    return _original_fstab(info, input_file, read_helper)


common._FindAndLoadRecoveryFstab = _device_fstab
ota_from_target_files.GenerateNonAbOtaPackage = GenerateNonAbOtaPackage

if __name__ == "__main__":
    try:
        common.CloseInheritedPipes()
        ota_from_target_files.main(sys.argv[1:])
    except common.ExternalError as error:
        sys.exit(str(error))
    finally:
        common.Cleanup()
