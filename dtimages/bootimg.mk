# Keep Samsung's boot format, but never let dtbTool scan mixed model trees.
include hardware/samsung/mkbootimg.mk

# INTERNAL_BOOTIMAGE_FILES omits the kernel in this branch's custom-boot path.
# DT restat may suppress a DT-only dependency even after zImage has changed.
$(INSTALLED_BOOTIMAGE_TARGET): $(INSTALLED_KERNEL_TARGET)

A3_DTIMAGE_BUILDER := device/samsung/a3lte/dtimages/build_dtimages.py
A3_DTIMAGE_OUTPUTS := $(addprefix $(PRODUCT_OUT)/,dtF.img dtFU.img dtH.img)

# A DTS-only change need not alter zImage's timestamp. Track include files
# explicitly, then let kernel Make rebuild only the affected DTBs.
A3_DTIMAGE_DTS_DEPS := $(addprefix $(TARGET_KERNEL_SOURCE)/,$(call find-files-in-subdirs,$(TARGET_KERNEL_SOURCE),"*.dts*",arch/arm/boot/dts))
A3_DTIMAGE_DTS_DEPS += $(addprefix $(TARGET_KERNEL_SOURCE)/,$(call find-files-in-subdirs,$(TARGET_KERNEL_SOURCE),"*.h",include/dt-bindings))

# Kati tracks all outputs of this one action. Removing any variant reruns it.
$(INSTALLED_DTIMAGE_TARGET): .KATI_IMPLICIT_OUTPUTS := $(A3_DTIMAGE_OUTPUTS)
.KATI_RESTAT: $(INSTALLED_DTIMAGE_TARGET)
$(INSTALLED_DTIMAGE_TARGET): $(A3_DTIMAGE_BUILDER) device/samsung/a3lte/dtimages/variants.mk $(A3_DTIMAGE_DTS_DEPS) $(KERNEL_CONFIG)

# Used by the inherited recipe. A cheap incremental dtbs check is necessary
# even when zImage was unchanged; this never compiles another kernel image.
define build-dtimage-target
    $(call pretty,"Target A3 DT images: F / FU / H")
    $(call make-kernel-target,dtbs)
    $(hide) python3 $(A3_DTIMAGE_BUILDER) \
        --dtb-dir $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/dts \
        --dtbtool $(DTBTOOL) --dtc-dir $(KERNEL_OUT)/scripts/dtc \
        --page-size $(BOARD_KERNEL_PAGESIZE) --output-dir $(PRODUCT_OUT) \
        --f $(A3_DTB_F) --fu $(A3_DTB_FU) --h $(A3_DTB_H)
endef

# RADIO is a target-files transport directory, not a request to flash modem.
# Releasetools copies only FU/H into the existing boot-repack operation.
INSTALLED_RADIOIMAGE_TARGET += $(A3_DTIMAGE_OUTPUTS)
