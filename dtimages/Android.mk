LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),a3lte)
# Bundle recovery-side tools with target-files as well: OTA generation must
# not depend on whichever source checkout happens to exist on its host.
$(foreach file,kernel_make.sh kernel_unpack.sh mkbootimg unpackbootimg,\
    $(eval $(call add-radio-file,../../../../vendor/samsung/a3lte/dtimages/$(file))))
endif
