# Select the device's stock Venus image without overriding other firmware.
PRODUCT_COPY_FILES += \
    device/samsung/a3lte/media/init.a3lte.venus.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.a3lte.venus.rc

# Keep the platform-facing AV1 component name/API, select the A3 native backend.
SOONG_CONFIG_NAMESPACES += a3_media
SOONG_CONFIG_a3_media += native_decoders
SOONG_CONFIG_a3_media_native_decoders := true
PRODUCT_PACKAGES += libcodec2_soft_av1dec_gav1 libcodec2_soft_vp9dec
