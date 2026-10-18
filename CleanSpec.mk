# SPDX-License-Identifier: Apache-2.0
# Remove generated recovery payloads left by previous incremental builds.
$(call add-clean-step, rm -f $(PRODUCT_OUT)/recovery.img $(PRODUCT_OUT)/ramdisk-recovery.img)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/recovery $(PRODUCT_OUT)/obj/PACKAGING/recovery_patch_intermediates)
$(call add-clean-step, rm -f $(PRODUCT_OUT)/system/vendor/bin/install-recovery.sh $(PRODUCT_OUT)/system/vendor/recovery-from-boot.p $(PRODUCT_OUT)/system/vendor/etc/recovery.img $(PRODUCT_OUT)/system/vendor/etc/recovery-resource.dat)
$(call add-clean-step, rm -f $(PRODUCT_OUT)/system/vendor/bin/applypatch $(PRODUCT_OUT)/system/vendor/etc/init/vendor_flash_recovery.rc $(PRODUCT_OUT)/root/init.recovery.qcom.rc)
$(call add-clean-step, rm -f $(PRODUCT_OUT)/system.img $(PRODUCT_OUT)/system.map $(PRODUCT_OUT)/vendor.img $(PRODUCT_OUT)/vendor.map $(PRODUCT_OUT)/obj/PACKAGING/systemimage_intermediates/system.img)
