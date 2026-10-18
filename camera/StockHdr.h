/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <hardware/camera.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "StockJpeg.h"

// Camera1 adapter for the Samsung A300FU SuperShot engine. The legacy Qualcomm
// HAL exposes native bracketing only through its Samsung command protocol.
class StockHdr {
public:
    using Complete = void (*)(void*, const std::vector<uint8_t>&);
    using Error = void (*)(void*);
    StockHdr(camera_device_t* vendor, Complete complete, Error error, void* user);
    ~StockHdr();
    bool available() const;
    bool active() const { return mActive.load(); }
    int takePicture();
    bool busy();
    void cancel();
    bool parameters(std::string* saved);
    bool onData(int32_t type, const camera_memory_t* data, unsigned index);
    bool onNotify(int32_t type);
    void enableMessages(int32_t types);
    void disableMessages(int32_t types);
    int messagesEnabled(int32_t types);

private:
    void capture(std::string parameters);
    bool fuse(const std::vector<uint8_t> (&pixels)[3], int quality, std::vector<uint8_t>* result);
    void captureMessages(bool enabled);
    bool cancelled();
    camera_device_t* mVendor;
    Complete mComplete;
    Error mError;
    void* mUser;
    void* mLibrary;
    std::atomic<bool> mActive{false};
    std::mutex mMutex;
    std::mutex mApiMutex;
    std::mutex mMessageMutex;
    uint32_t mMessages = 0;
    bool mCaptureMessages = false;
    bool mDelivering = false;
    std::condition_variable mCondition;
    std::thread mWorker;
    bool mCancelled = false;
    bool mFailed = false;
    bool mWaiting = false;
    bool mShutter = false;
    int mWidth = 0, mHeight = 0;
    unsigned mCount = 0;
    std::vector<uint8_t> mFrames[3];
    std::vector<uint8_t> mAeDebug;
    StockJpeg mEncoder;
    std::string mParameters;
};
