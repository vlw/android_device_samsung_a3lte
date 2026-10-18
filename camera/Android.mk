LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    system/media/camera/include

LOCAL_SRC_FILES := \
    CameraWrapper.cpp \
    StockHdr.cpp \
    StockJpeg.cpp \
    StockExif.cpp

LOCAL_HEADER_LIBRARIES += \
    libnativebase_headers \
    libnativewindow_headers

LOCAL_STATIC_LIBRARIES := libbase libarect
LOCAL_SHARED_LIBRARIES := \
    libhardware liblog libcamera_client libutils libcutils libdl libsecjpeginterface libexif \
    android.hidl.token@1.0-utils \
    android.hardware.graphics.bufferqueue@1.0 \
    android.hardware.graphics.bufferqueue@2.0

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/hw
LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

# Explicit diagnostic target; not included in PRODUCT_PACKAGES.
include $(CLEAR_VARS)
LOCAL_MODULE := a3_camera_vendor_probe
LOCAL_SRC_FILES := tests/CameraVendorProbe.cpp
LOCAL_SHARED_LIBRARIES := libhardware libcamera_client libutils libdl
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := a3_stock_hdr_probe
LOCAL_SRC_FILES := tests/StockHdrProbe.cpp
LOCAL_SHARED_LIBRARIES := libdl
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

# Sensor-free regression test of capture lifecycle and the stock HDR ABI.
include $(CLEAR_VARS)
LOCAL_MODULE := a3_hdr_lifecycle_test
LOCAL_SRC_FILES := tests/HdrLifecycleTest.cpp StockHdr.cpp StockJpeg.cpp StockExif.cpp
LOCAL_SHARED_LIBRARIES := libcamera_client libutils libcutils liblog libdl libjpeg libsecjpeginterface libexif
LOCAL_HEADER_LIBRARIES := libnativebase_headers libnativewindow_headers
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

# Minimal original SecJpegEncoder base ABI for the unmodified Samsung board
# encoder. Avoid loading the Android 6 UI/Skia stack into Android 12 mediaserver.
include $(CLEAR_VARS)
LOCAL_MODULE := libsecjpeginterface
LOCAL_SRC_FILES := SecJpegAbi.cpp
LOCAL_SHARED_LIBRARIES := libdl
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := a3_stock_jpeg_test
LOCAL_SRC_FILES := tests/StockJpegTest.cpp StockJpeg.cpp
LOCAL_SHARED_LIBRARIES := libsecjpeginterface libjpeg libdl liblog
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := a3_camera_blob_load_test
LOCAL_SRC_FILES := tests/CameraBlobLoadTest.cpp
LOCAL_C_INCLUDES := system/media/camera/include
LOCAL_SHARED_LIBRARIES := libdl libhardware
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

# Firmware oracle; explicit sensor-free diagnostic, never installed in images.
include $(CLEAR_VARS)
LOCAL_MODULE := a3_stock_exif_oracle
LOCAL_SRC_FILES := tests/StockExifOracle.cpp
LOCAL_SHARED_LIBRARIES := libcamera_client libutils libdl
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := a3_stock_exif_test
LOCAL_SRC_FILES := tests/StockExifTest.cpp StockExif.cpp
LOCAL_SHARED_LIBRARIES := libcamera_client libutils libcutils libexif
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

# Exercise the production wrapper against a fake HAL without opening sensors.
include $(CLEAR_VARS)
LOCAL_MODULE := a3_camera_wrapper_test
LOCAL_SRC_FILES := tests/CameraWrapperTest.cpp StockHdr.cpp StockJpeg.cpp StockExif.cpp
LOCAL_C_INCLUDES := system/media/camera/include
LOCAL_HEADER_LIBRARIES := libnativebase_headers libnativewindow_headers
LOCAL_STATIC_LIBRARIES := libbase libarect
LOCAL_SHARED_LIBRARIES := libhardware liblog libcamera_client libutils libcutils libdl libsecjpeginterface libexif
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)
