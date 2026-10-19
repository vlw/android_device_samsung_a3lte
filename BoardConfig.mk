#
# Copyright (C) 2015 The CyanogenMod Project
# Copyright (C) 2018 The LineageOS Project
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
#

FORCE_32_BIT := true

# Inherit from msm8916-common
include device/samsung/msm8916-common/BoardConfigCommon.mk

DEVICE_PATH := device/samsung/a3lte

# Audio
USE_CUSTOM_MIXER_PATHS := true
USE_CUSTOM_AUDIO_PLATFORM_INFO := true

# Bluetooth
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

# Filesystem
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 13631488
# SM-A300FU has a 200 MiB cache partition. Do not build a larger image.
BOARD_CACHEIMAGE_PARTITION_SIZE := 209715200
# The partition named persist holds raw FRP data, never an ext4 image.
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 15728640
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1568669696
BOARD_USERDATAIMAGE_PARTITION_SIZE := 10537124096
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4

# Init
TARGET_INIT_VENDOR_LIB := //device/samsung/a3lte:libinit_a3
TARGET_RECOVERY_DEVICE_MODULES := libinit_a3

# Kernel
TARGET_KERNEL_CONFIG := lineageos_a3_defconfig

# Power
TARGET_HAS_NO_POWER_STATS := true

# Properties
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/system.prop

TARGET_RELEASETOOLS_EXTENSIONS := device/samsung/a3lte/releasetools

# Keep the Samsung custom boot builder inherited from msm8916-common, but
# package only its finished boot image. BOARD_CUSTOM_BOOTIMG also copies an
# unconditional recovery.img in this branch, which is absent on this device.
BOARD_CUSTOM_BOOTIMG :=
BOARD_COPY_BOOT_IMAGE_TO_TARGET_FILES := true

# Recovery is owned by the separately installed TWRP, for every build variant.
# Explicit PRODUCT_BUILD_RECOVERY_IMAGE=true overrides TARGET_NO_RECOVERY in AOSP.
override TARGET_NO_RECOVERY := true
ifeq ($(PRODUCT_BUILD_RECOVERY_IMAGE),true)
$(error a3lte must not build or package a recovery image)
endif
ifneq ($(filter true,$(BOARD_USES_RECOVERY_AS_BOOT) $(BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT) $(BOARD_INCLUDE_RECOVERY_RAMDISK_IN_VENDOR_BOOT)),)
$(error a3lte must not embed recovery in boot or vendor_boot)
endif

# Keep the partition map for OTA tools/TWRP; do not build a recovery image.
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.qcom

# SELinux
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy

# TWRP
ifeq ($(WITH_TWRP),true)
include $(DEVICE_PATH)/twrp.mk
endif

#Light
TARGET_PROVIDES_LIBLIGHT := true

# Inherit from proprietary files
include vendor/samsung/a3lte/BoardConfigVendor.mk
