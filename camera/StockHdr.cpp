/* SPDX-License-Identifier: Apache-2.0 */
#define LOG_TAG "A3StockHdr"
#include "StockHdr.h"
#include "StockExif.h"

#include <camera/CameraParameters.h>
#include <dlfcn.h>
#include <log/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/String8.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>

namespace {
using android::CameraParameters;
using android::String8;
using namespace std::chrono_literals;
// ABI recovered from A300FUXXU1CSG3 libseccameracore ShotHDR. Keep 32-bit
// layout: width, height, reserved, pitch, format (9 = NV21), data pointer.
struct Image {
    int width, height, reserved, pitch, format;
    uint8_t* data;
};
struct Params {
    int height, width, registration, uiMode, type, scratch, useScratch;
};
static_assert(sizeof(Image) == 24, "Samsung HDR is a 32-bit ABI");
using Progress = int (*)(int, int, void*);
using Create = int (*)(void**);
using Init = int (*)(void*, const Params*, const char*);
using Run = int (*)(void*, int, Image**, int, Image*, void*, Progress, void*);
using Release = int (*)(void*);
int progress(int, int, void*) { return 0; }

}  // namespace

StockHdr::StockHdr(camera_device_t* vendor, Complete complete, Error error, void* user)
    : mVendor(vendor),
      mComplete(complete),
      mError(error),
      mUser(user),
      mLibrary(dlopen("/vendor/lib/libsupershot.so", RTLD_NOW | RTLD_LOCAL)) {
    if (mLibrary && (!dlsym(mLibrary, "SS_Create") || !dlsym(mLibrary, "SS_Init") ||
                     !dlsym(mLibrary, "SS_Run") || !dlsym(mLibrary, "SS_Release"))) {
        dlclose(mLibrary);
        mLibrary = nullptr;
    }
    if (!mLibrary) ALOGW("Stock HDR unavailable: %s", dlerror());
    if (mVendor->ops->msg_type_enabled)
        for (unsigned bit = 0; bit < 32; ++bit)
            if (mVendor->ops->msg_type_enabled(mVendor, uint32_t{1} << bit))
                mMessages |= uint32_t{1} << bit;
}
StockHdr::~StockHdr() {
    cancel();
    // close/release may wait for callback completion after the framework has
    // disabled its messages. stop_preview/cancel must not join that callback:
    // CameraClient can hold the very lock the callback is trying to acquire.
    if (mWorker.joinable()) mWorker.join();
    if (mLibrary) dlclose(mLibrary);
}
bool StockHdr::available() const { return mLibrary != nullptr && mEncoder.available(); }
bool StockHdr::busy() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mActive || mDelivering;
}
int StockHdr::takePicture() {
    if (!available()) return -ENOSYS;
    std::lock_guard<std::mutex> apiLock(mApiMutex);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mActive || mDelivering) return -EBUSY;
    }
    if (mWorker.joinable()) mWorker.join();
    char* raw = mVendor->ops->get_parameters(mVendor);
    if (!raw) return -EINVAL;
    std::string parameters(raw);
    CameraParameters params{String8(raw)};
    int width, height;
    params.getPictureSize(&width, &height);
    mVendor->ops->put_parameters(mVendor, raw);
    int quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (quality < 1 || quality > 100) return -EINVAL;
    if (width < 64 || height < 64 || width > 3264 || height > 3264 || (width & 1) || (height & 1))
        return -EINVAL;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mWidth = width;
        mHeight = height;
        mCount = 0;
        mCancelled = mFailed = mShutter = mWaiting = false;
        mParameters = parameters;
        for (auto& frame : mFrames) frame.clear();
        mAeDebug.clear();
        mActive = true;
    }
    mWorker = std::thread(&StockHdr::capture, this, parameters);
    return 0;
}
bool StockHdr::parameters(std::string* saved) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mActive) return false;
    *saved = mParameters;
    return true;
}
void StockHdr::cancel() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mCancelled = true;
        mCondition.notify_all();
        if (mDelivering) return;
    }
    std::lock_guard<std::mutex> apiLock(mApiMutex);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mCancelled = true;
        mCondition.notify_all();
        if (mDelivering) return;
    }
    if (mWorker.joinable()) mWorker.join();
}
bool StockHdr::cancelled() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mCancelled;
}
void StockHdr::enableMessages(int32_t types) {
    std::lock_guard<std::mutex> lock(mMessageMutex);
    mMessages |= static_cast<uint32_t>(types);
    mVendor->ops->enable_msg_type(mVendor, types);
}
void StockHdr::disableMessages(int32_t types) {
    std::lock_guard<std::mutex> lock(mMessageMutex);
    mMessages &= ~static_cast<uint32_t>(types);
    if (mCaptureMessages) types &= ~CAMERA_MSG_COMPRESSED_IMAGE;
    if (types) mVendor->ops->disable_msg_type(mVendor, types);
}
int StockHdr::messagesEnabled(int32_t types) {
    std::lock_guard<std::mutex> lock(mMessageMutex);
    return (mMessages & static_cast<uint32_t>(types)) != 0;
}
void StockHdr::captureMessages(bool enabled) {
    std::lock_guard<std::mutex> lock(mMessageMutex);
    mCaptureMessages = enabled;
    if (enabled || (mMessages & CAMERA_MSG_COMPRESSED_IMAGE))
        mVendor->ops->enable_msg_type(mVendor, CAMERA_MSG_COMPRESSED_IMAGE);
    else
        mVendor->ops->disable_msg_type(mVendor, CAMERA_MSG_COMPRESSED_IMAGE);
}
bool StockHdr::onData(int32_t type, const camera_memory_t* data, unsigned index) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mActive) return false;
    if (type == CAMERA_MSG_COMPRESSED_IMAGE && mWaiting) {
        // Samsung's raw HDR path reports packed NV21 with the legacy JPEG
        // message ID. processRawImageImpl has already removed chroma padding.
        size_t bytes = static_cast<size_t>(mWidth) * mHeight * 3 / 2;
        if (data && data->data && index == 0 && data->size >= bytes && mCount < 3) {
            const auto* first = static_cast<const uint8_t*>(data->data);
            mFrames[mCount].assign(first, first + bytes);
            mAeDebug = cameraAeDebug(first, data->size);
            ++mCount;
        } else
            mFailed = true;
        if (mCount == 3 || mFailed) mWaiting = false;
        mCondition.notify_all();
    }
    return type == CAMERA_MSG_COMPRESSED_IMAGE || type == CAMERA_MSG_RAW_IMAGE ||
           type == CAMERA_MSG_POSTVIEW_FRAME || type == CAMERA_MSG_RAW_IMAGE_NOTIFY;
}
bool StockHdr::onNotify(int32_t type) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mActive) return false;
    if (type == CAMERA_MSG_ERROR) {
        mFailed = true;
        mCondition.notify_all();
        return true;
    }
    if (type == CAMERA_MSG_SHUTTER) {
        bool suppress = mShutter;
        mShutter = true;
        return suppress;
    }
    return type == CAMERA_MSG_RAW_IMAGE_NOTIFY;
}
void StockHdr::capture(std::string saved) {
    CameraParameters params{String8(saved.c_str())};
    std::vector<uint8_t> frames[3], result, aeDebug;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mWaiting = !mCancelled;
    }
    // hdr-mode=1 and command 1014 are configured before preview, like ShotHDR.
    // The HAL produces one native -2/0/+2 EV series. Do not change EV/AWB or
    // capture independent JPEGs: that changes the stock exposure/ISP pipeline.
    bool success;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        success = !mCancelled;
    }
    captureMessages(true);
    if (success) success = mVendor->ops->take_picture(mVendor) == 0;
    if (success) {
        std::unique_lock<std::mutex> lock(mMutex);
        bool ready =
            mCondition.wait_for(lock, 15s, [this] { return !mWaiting || mCancelled || mFailed; });
        success = ready && !mCancelled && !mFailed && mCount == 3;
        if (success) {
            for (int i = 0; i < 3; ++i) frames[i].swap(mFrames[i]);
            aeDebug.swap(mAeDebug);
        }
    }
    mVendor->ops->cancel_picture(mVendor);
    mVendor->ops->stop_preview(mVendor);
    // No temporary user EV or AWB modifications were made. Keep native HDR
    // configured so the client's next start_preview remains in the same mode.
    if (success && cancelled()) success = false;
    if (success) success = fuse(frames, params.getInt(CameraParameters::KEY_JPEG_QUALITY), &result);
    if (success && cancelled()) success = false;
    if (success) {
        // Stock sendDataCallbackToApp obtains current HAL metadata after JPEG
        // encoding. Keep the pre-capture parameter snapshot private to clients.
        char* current = mVendor->ops->get_parameters(mVendor);
        success = addCameraExif(current ? current : saved, &result, aeDebug);
        if (current) mVendor->ops->put_parameters(mVendor, current);
    }
    captureMessages(false);
    bool deliver;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        deliver = !mCancelled;
        mWaiting = false;
        mDelivering = deliver;
        mActive = false;
    }
    if (deliver) {
        if (success) {
            if (messagesEnabled(CAMERA_MSG_COMPRESSED_IMAGE)) mComplete(mUser, result);
        } else {
            ALOGE("Native HDR capture failed, received %u/3 NV21 frames", mCount);
            if (messagesEnabled(CAMERA_MSG_ERROR)) mError(mUser);
        }
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mDelivering = false;
    }
}
bool StockHdr::fuse(const std::vector<uint8_t> (&frames)[3], int quality,
                    std::vector<uint8_t>* result) {
    std::vector<uint8_t> output(mWidth * mHeight * 2, 0);
    Image images[3], *inputs[3];
    for (int i = 0; i < 3; ++i) {
        if (frames[i].size() != static_cast<size_t>(mWidth) * mHeight * 3 / 2) return false;
        images[i] = {mWidth, mHeight, 0, mWidth, 9, const_cast<uint8_t*>(frames[i].data())};
        inputs[i] = &images[i];
    }
    Image out = {mWidth, mHeight, 0, mWidth, 9, output.data()};
    Params params = {out.height, out.width, 1, 3, 4, 0, 0};
    void* handle = nullptr;
    int rc = reinterpret_cast<Create>(dlsym(mLibrary, "SS_Create"))(&handle);
    if (!rc) rc = reinterpret_cast<Init>(dlsym(mLibrary, "SS_Init"))(handle, &params, nullptr);
    uint8_t info[4096] = {};
    if (!rc)
        rc = reinterpret_cast<Run>(dlsym(mLibrary, "SS_Run"))(handle, 0, inputs, 3, &out, info,
                                                              progress, this);
    if (handle) reinterpret_cast<Release>(dlsym(mLibrary, "SS_Release"))(handle);
    ALOGI("Stock SuperShot HDR: %dx%d, 3 native NV21 exposures, result=%d", out.width, out.height,
          rc);
    return rc == 0 && mEncoder.encode(out.data, out.width, out.height, quality, result);
}
