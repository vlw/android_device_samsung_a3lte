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

# Per-device media profiles must be selected before inheriting the common config.
DEVICE_MEDIA_PROFILES := device/samsung/a3lte/configs/media_profiles_V1_0.xml
DEVICE_CAMERA_DAEMON := vendor/samsung/a3lte/proprietary/vendor/bin/mm-qcamera-daemon
DEVICE_CAMERA_LIBS := vendor/samsung/a3lte/proprietary/vendor/lib

# Keep the original Samsung radio behind the device IMS-state adaptor.
DEVICE_RIL_LIBPATH := /vendor/lib/liba3ims_radio.so

# Signed Venus stays on the device firmware partition for every A3 variant.
DEVICE_USES_NATIVE_VENUS_FIRMWARE := true

# Inherit from msm8916-common
$(call inherit-product, device/samsung/msm8916-common/msm8916.mk)

# Soong
PRODUCT_SOONG_NAMESPACES += \
    device/samsung/a3lte \
    hardware/google/pixel \
    hardware/google/interfaces

# Export Power HAL dependency namespaces to Make so its shared libraries are
# installed as well as linked. Missing these blocks PowerManager at boot.
PRODUCT_PACKAGES += libperfmgr pixel-power-ext-V1-ndk_platform

# Overlay
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := hdpi

# CSC features (read by the patched libsecnativefeature from /vendor/etc)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/csc/others.xml:$(TARGET_COPY_OUT_VENDOR)/etc/others.xml

# Audio
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/acdb/Bluetooth_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/Bluetooth_cal.acdb \
    $(LOCAL_PATH)/configs/audio/acdb/General_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/General_cal.acdb \
    $(LOCAL_PATH)/configs/audio/acdb/Global_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/Global_cal.acdb \
    $(LOCAL_PATH)/configs/audio/acdb/Handset_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/Handset_cal.acdb \
    $(LOCAL_PATH)/configs/audio/acdb/Hdmi_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/Hdmi_cal.acdb \
    $(LOCAL_PATH)/configs/audio/acdb/Headset_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/Headset_cal.acdb \
    $(LOCAL_PATH)/configs/audio/acdb/Speaker_cal.acdb:$(TARGET_COPY_OUT_VENDOR)/etc/acdbdata/Speaker_cal.acdb

# Audio configuration
PRODUCT_COPY_FILES += \
	$(LOCAL_PATH)/configs/audio/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml

# Audio Platform info config
PRODUCT_COPY_FILES += \
	$(LOCAL_PATH)/configs/audio/audio_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info.xml

PRODUCT_PACKAGES += audio_amplifier.msm8916 A3TfaVolume

# Correct normalized AIDL vibration amplitudes for the ss_vibrator driver.
PRODUCT_PACKAGES += android.hardware.vibrator-service.a3lte

# Shipping API level
$(call inherit-product, $(SRC_TARGET_DIR)/product/product_launched_with_k.mk)

# Boot animation
TARGET_SCREEN_HEIGHT := 960
TARGET_SCREEN_WIDTH := 540

# Camera
PRODUCT_PACKAGES += \
    camera.msm8916 \
    Snap

PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-impl \
    camera.device@1.0-impl

# RIL
PRODUCT_PACKAGES += \
	libril \
	librilutils \
	rild

# Display
PRODUCT_PACKAGES += \
    libjni_livedisplay

# Display Device Config
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/display_id_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/displayconfig/display_id_0.xml

# Init scripts
PRODUCT_PACKAGES += \
    fstab.qcom \
    init.target.rc

# NFC
PRODUCT_COPY_FILES += \
	$(LOCAL_PATH)/configs/nfc/nfcee_access.xml:$(TARGET_COPY_OUT_VENDOR)/etc/nfcee_access.xml \
	$(LOCAL_PATH)/configs/nfc/libnfc-nci.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-nci.conf \
	$(LOCAL_PATH)/configs/nfc/libnfc-sec.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-sec.conf \
	$(LOCAL_PATH)/configs/nfc/libnfc-sec-hal.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-sec-hal.conf

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml

PRODUCT_PACKAGES += \
	libnfc-nci \
	libnfc_nci_jni \
	NfcNci \
	Tag \
	com.android.nfc_extras \
	android.hardware.nfc@1.0-service.samsung

# USB ID
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    ro.usb.id.midi=90BA \
    ro.usb.id.midi_adb=90BB \
    ro.usb.id.mtp=300B \
    ro.usb.id.mtp_adb=300C \
    ro.usb.id.ptp=300D \
    ro.usb.id.ptp_adb=300E \
    ro.usb.id.ums=300F \
    ro.usb.id.ums_adb=3010 \
    ro.usb.vid=2a96

# Call the proprietary setup
$(call inherit-product, vendor/samsung/a3lte/a3lte-vendor.mk)

# IMS / VoLTE
$(call inherit-product, device/samsung/a3lte/ims/ims.mk)

# Never replace the user's separately installed TWRP.
PRODUCT_BUILD_RECOVERY_IMAGE := false
PRODUCT_PACKAGES += a3lte_no_recovery_update
PRODUCT_VENDOR_PROPERTIES += persist.vendor.recovery_update=false
PRODUCT_HOST_PACKAGES += a3_ota_from_target_files

# Device-specific signed video firmware and selection policy.
$(call inherit-product, device/samsung/a3lte/media/media.mk)

# Real sensors for the legacy Thermal 1.0 service.
PRODUCT_PACKAGES += thermal.a3lte
PRODUCT_VENDOR_PROPERTIES += ro.hardware.thermal=a3lte

# Required by debug.composition.type=c2d in msm8916-common/system.prop.
PRODUCT_PACKAGES += copybit.msm8916

# Match tethering acceleration capabilities to the legacy kernel.
PRODUCT_PACKAGES += A3lteTetheringOverlay
