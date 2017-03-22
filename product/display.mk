# Boot animation
TARGET_SCREEN_HEIGHT := 1280
TARGET_SCREEN_WIDTH := 720

# Display
PRODUCT_PACKAGES += \
    libjni_livedisplay \
    pp_calib_data_nt35521_dijing_4p7inch_hd_video_panel.xml \
    svi_config.xml

PRODUCT_PROPERTY_OVERRIDES += \
    ro.qualcomm.cabl=0 \
    ro.qualcomm.svi=1 \
    config.svi.xml=1 \
    config.svi.path=/system/etc/svi_config.xml

# HWUI
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hwui.text_large_cache_height=2048

# Screen density
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=320

PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapgrowthlimit=128m \
    dalvik.vm.heapminfree=6m \
    dalvik.vm.heapstartsize=14m
$(call inherit-product, frameworks/native/build/phone-xhdpi-1024-dalvik-heap.mk)
