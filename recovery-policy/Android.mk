# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)
ifeq ($(TARGET_DEVICE),a3lte)
include $(CLEAR_VARS)
LOCAL_MODULE := a3lte_no_recovery_update
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := zz_a3lte_no_recovery_update.rc
LOCAL_MODULE_STEM := zz_a3lte_no_recovery_update.rc
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_ETC)/init
# base_vendor includes these unconditionally. Remove them from this product's
# build/install dependency closure, including applypatch's flash-recovery init rc.
LOCAL_OVERRIDES_MODULES := \
    applypatch init.recovery.qcom.rc recovery adbd.recovery \
    android.hardware.health@2.0-impl-default.recovery cgroups.recovery.json \
    charger.recovery init_second_stage.recovery ld.config.recovery.txt \
    linker.recovery otacerts.recovery shell_and_utilities_recovery watchdogd.recovery
include $(BUILD_PREBUILT)
endif

# Host resources refer directly to the maintained Lineage files. They are never
# installed on the phone or copied into the device source tree.
ifeq ($(TARGET_DEVICE),a3lte)
include $(CLEAR_VARS)
LOCAL_MODULE := a3_ota_backuptool
LOCAL_MODULE_CLASS := ETC
LOCAL_IS_HOST_MODULE := true
LOCAL_SRC_FILES := ../../../../vendor/lineage/prebuilt/common/bin/backuptool.sh
LOCAL_MODULE_STEM := backuptool.sh
LOCAL_MODULE_PATH := $(HOST_OUT)/etc/a3-ota
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := a3_ota_backuptool_functions
LOCAL_MODULE_CLASS := ETC
LOCAL_IS_HOST_MODULE := true
LOCAL_SRC_FILES := ../../../../vendor/lineage/prebuilt/common/bin/backuptool.functions
LOCAL_MODULE_STEM := backuptool.functions
LOCAL_MODULE_PATH := $(HOST_OUT)/etc/a3-ota
include $(BUILD_PREBUILT)
endif
