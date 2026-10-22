#
# Copyright (C) 2015 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE), a3lte)

# A3_NATIVE_FIRMWARE_GUARD_BEGIN
# Signed subsystem/TEE images must match the device firmware partitions.
A3LTE_PARTITION_IMAGE_NAMES := venus.% cmnlib.% keymaster.% keymaste.% \
    modem.% mba.mbn wcnss.% pronto.% adsp.%
A3LTE_SIGNED_FIRMWARE_COPIES := $(strip $(foreach fwcopy,$(PRODUCT_COPY_FILES),\
    $(if $(filter $(A3LTE_PARTITION_IMAGE_NAMES),$(notdir $(word 2,$(subst :, ,$(fwcopy))))),$(fwcopy))))
ifneq ($(A3LTE_SIGNED_FIRMWARE_COPIES),)
$(error a3lte must use native partition firmware, remove PRODUCT_COPY_FILES: $(A3LTE_SIGNED_FIRMWARE_COPIES))
endif
# A3_NATIVE_FIRMWARE_GUARD_END

# AOSP non-A/B packaging requires and bundles recovery. Use the device-owned
# generator that preserves TWRP and rejects recovery-bearing inputs instead.
# core/Makefile otherwise disables non-A/B OTA entirely when recovery is off.
# This is metadata only; the runtime copy supplies the host generator's map.
recovery_fstab := $(TARGET_RECOVERY_FSTAB)
OTA_FROM_TARGET_FILES := $(HOST_OUT_EXECUTABLES)/a3_ota_from_target_files$(HOST_EXECUTABLE_SUFFIX)

include $(call all-subdir-makefiles,$(LOCAL_PATH))

endif
