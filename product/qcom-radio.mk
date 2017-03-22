# Properties
PRODUCT_PROPERTY_OVERRIDES += \
    persist.radio.multisim.config=dsds \
    ril.ecclist=000,08,100,101,102,110,112,118,119,120,122,911,999 \
    ril.ecclist1=000,08,100,101,102,110,112,118,119,120,122,911,999 \
    rild.libpath=/system/vendor/lib/libril-qc-qmi-1.so \
    ro.config.always_show_roaming=true \
    ro.telephony.default_network=9,9
