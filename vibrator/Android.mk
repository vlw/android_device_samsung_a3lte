# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)
ifeq ($(TARGET_DEVICE),a3lte)
include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.vibrator-service.a3lte
# Reuse the Samsung implementation with an a3lte amplitude adapter.
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_OVERRIDES_MODULES := android.hardware.vibrator-service.samsung
LOCAL_SRC_FILES := A3Vibrator.cpp service.cpp ../../../../hardware/samsung/aidl/vibrator/Vibrator.cpp
LOCAL_C_INCLUDES := hardware/samsung/aidl/vibrator
LOCAL_SHARED_LIBRARIES := libbase libbinder_ndk android.hardware.vibrator-V2-ndk_platform
LOCAL_INIT_RC := android.hardware.vibrator-service.a3lte.rc
LOCAL_VINTF_FRAGMENTS := android.hardware.vibrator-service.a3lte.xml
LOCAL_CPPFLAGS := -Wall -Wextra -Werror
include $(BUILD_EXECUTABLE)

# Explicit diagnostics only; not installed by PRODUCT_PACKAGES.
include $(CLEAR_VARS)
LOCAL_MODULE := a3_vibrator_test
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := test.cpp
LOCAL_SHARED_LIBRARIES := libbinder_ndk android.hardware.vibrator-V2-ndk_platform
LOCAL_CPPFLAGS := -Wall -Wextra -Werror
include $(BUILD_EXECUTABLE)
endif
