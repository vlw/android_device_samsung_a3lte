# SPDX-License-Identifier: Apache-2.0
"""Small, fail-closed adapter for the branch's existing non-A/B OTA generator.

Reuse upstream functions rather than maintaining their copies. Only four exact
recovery-specific fragments are excluded, in memory; sources on disk stay intact.
If upstream changes these fragments, stop and require review of this adapter.
"""
import inspect
import os
import zipfile
import common
import non_ab_ota
import ota_utils
from recovery_policy import check_target, check_ota


def _drop_once(source, fragment):
    if source.count(fragment) != 1:
        raise common.ExternalError("a3lte: OTA generator changed; review no-recovery adapter")
    return source.replace(fragment, "", 1)


def _no_two_step():
    if common.OPTIONS.two_step:
        raise common.ExternalError("a3lte: two-step OTA would overwrite TWRP")


def install():
    # Prepare and validate every edit before changing any upstream function.
    edits = {
        "GenerateNonAbOtaPackage": [
            '  if OPTIONS.info_dict.get("no_recovery") == "true":\n'
            '    raise common.ExternalError(\n'
            '        "--- target build has specified no recovery ---")\n'],
        "WriteFullOTAPackage": [
            '  assert HasRecoveryPatch(input_zip, info_dict=OPTIONS.info_dict)\n',
            '  recovery_img = common.GetBootableImage("recovery.img", "recovery.img",\n'
            '                                         OPTIONS.input_tmp, "RECOVERY")\n'
            '  common.CheckSize(recovery_img.data, "recovery.img", target_info)\n'
            '  common.ZipWriteStr(output_zip, "recovery.img", recovery_img.data)\n'],
        "WriteBlockIncrementalOTAPackage": [
            '  target_recovery = common.GetBootableImage(\n'
            '      "/tmp/recovery.img", "recovery.img", OPTIONS.target_tmp, "RECOVERY")\n'],
    }
    prepared = []
    for name, fragments in edits.items():
        source = inspect.getsource(getattr(non_ab_ota, name))
        for fragment in fragments:
            source = _drop_once(source, fragment)
        prepared.append(compile(source, "<a3lte no-recovery: " + name + ">", "exec"))
    # This branch writes serialized protobuf bytes to a text-mode sidecar.
    # Fix only the device's host tool; preserve upstream ZIP and legacy output.
    source = inspect.getsource(ota_utils.WriteMetadata)
    text_open = "with open('{}.pb'.format(output), 'w') as f:"
    if source.count(text_open) != 1:
        raise common.ExternalError("a3lte: review OTA metadata binary-write adapter")
    source = source.replace(text_open, text_open.replace("'w'", "'wb'"), 1)
    metadata_code = compile(source, "<a3lte binary OTA metadata>", "exec")
    exec(metadata_code, ota_utils.__dict__)

    for code in prepared:
        exec(code, non_ab_ota.__dict__)

    generate = non_ab_ota.GenerateNonAbOtaPackage
    def checked_generate(target_file, output_file, source_file=None):
        _no_two_step()
        if common.OPTIONS.info_dict.get("no_recovery") != "true":
            raise common.ExternalError("a3lte: target must disable recovery")
        with zipfile.ZipFile(target_file) as target:
            check_target(target)
        return generate(target_file, output_file, source_file)
    non_ab_ota.GenerateNonAbOtaPackage = checked_generate

    # Guard direct writer calls too: removed recovery variables only occur in
    # upstream's two-step branches, which must never be entered on this device.
    def guard(writer):
        def checked(*args, **kwargs):
            _no_two_step()
            return writer(*args, **kwargs)
        return checked
    non_ab_ota.WriteFullOTAPackage = guard(non_ab_ota.WriteFullOTAPackage)
    non_ab_ota.WriteBlockIncrementalOTAPackage = guard(non_ab_ota.WriteBlockIncrementalOTAPackage)

    finalize = non_ab_ota.FinalizeMetadata
    def checked_finalize(metadata, staging_file, output_file, *args, **kwargs):
        with zipfile.ZipFile(staging_file) as package:
            check_ota(package)
        return finalize(metadata, staging_file, output_file, *args, **kwargs)
    non_ab_ota.FinalizeMetadata = checked_finalize

    copy_install_tools = non_ab_ota.CopyInstallTools
    def copy_tools(output_zip):
        # AOSP omits INSTALL/ without recovery. Use the live Lineage sources,
        # installed as host resources by Android.mk, not copies in this tree.
        if common.OPTIONS.backuptool:
            for name in ("backuptool.sh", "backuptool.functions"):
                if not os.path.isfile(os.path.join(common.OPTIONS.input_tmp, "INSTALL/bin", name)):
                    path = os.path.join(common.OPTIONS.search_path, "etc/a3-ota", name)
                    with open(path, "rb") as source:
                        common.ZipWriteStr(output_zip, "install/bin/" + name, source.read())
        copy_install_tools(output_zip)
    non_ab_ota.CopyInstallTools = copy_tools
