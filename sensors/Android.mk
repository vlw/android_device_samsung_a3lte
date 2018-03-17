LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := sensors.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

LOCAL_CFLAGS += -DLOG_TAG=\"Sensors\"

LOCAL_C_INCLUDES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SRC_FILES := \
    sensors.cpp \
    SensorBase.cpp \
    LightSensor.cpp \
    ProximitySensor.cpp \
    Accelerometer.cpp \
    InputEventReader.cpp \
    CalibrationManager.cpp \
    NativeSensorManager.cpp \
    VirtualSensor.cpp \
    sensors_XML.cpp

LOCAL_C_INCLUDES += \
    external/libxml2/include \
    external/icu/icu4c/source/common

LOCAL_SHARED_LIBRARIES := liblog libcutils libdl libxml2 libutils

include $(BUILD_SHARED_LIBRARY)
