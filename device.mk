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

# Inherit from msm8916-common
$(call inherit-product, device/samsung/msm8916-common/msm8916.mk)

# Overlay
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := hdpi

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

PRODUCT_PACKAGES += audio_amplifier.msm8916

# Boot animation
TARGET_SCREEN_HEIGHT := 960
TARGET_SCREEN_WIDTH := 540

# Camera
PRODUCT_PACKAGES += \
    camera.msm8916 \
    libmm-qcamera \
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

# Init scripts
PRODUCT_PACKAGES += \
    fstab.qcom \
    init.target.rc

# Lights
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service \
    lights.msm8916

# NFC
PRODUCT_COPY_FILES += \
	$(LOCAL_PATH)/configs/nfc/nfcee_access.xml:$(TARGET_COPY_OUT_VENDOR)/etc/nfcee_access.xml \
	$(LOCAL_PATH)/configs/nfc/libnfc-nci.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-nci.conf \
	$(LOCAL_PATH)/configs/nfc/libnfc-sec.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-sec.conf \
	$(LOCAL_PATH)/configs/nfc/libnfc-sec-hal.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-sec-hal.conf

PRODUCT_COPY_FILES += \
    frameworks/base/nfc-extras/com.android.nfc_extras.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.android.nfc_extras.xml \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml

PRODUCT_PACKAGES += \
	libnfc-nci \
	libnfc_nci_jni \
	NfcNci \
	Tag \
	com.android.nfc_extras \
	android.hardware.nfc@1.0-impl \
	android.hardware.nfc@1.0-service \
    vendor.samsung.hardware.nfc@1.0-service.rc

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

# KeyStore
PRODUCT_PACKAGES += \
    keystore.qcom

# Call the proprietary setup
$(call inherit-product, vendor/samsung/a3lte/a3lte-vendor.mk)
