# SPDX-License-Identifier: Apache-2.0
PRODUCT_PACKAGES += \
    A3Ims \
    libevs_float \
    liba3ims_radio \
    A3ImsFrameworkOverlay \
    A3ImsTelephonyOverlay \
    A3ImsCarrierOverlay

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/android.hardware.telephony.ims.xml \
    device/samsung/a3lte/ims/privapp-permissions-me.phh.ims.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/privapp-permissions-me.phh.ims.xml

# Add the dedicated IMS APN without replacing the default Internet/MMS list.
CUSTOM_APNS_FILE := device/samsung/a3lte/ims/apns-custom.xml
