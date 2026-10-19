/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.nfc@1.0-service.samsung"

#include <log/log.h>

#include <hardware/hardware.h>
#include <hardware/nfc.h>

#include "Nfc.h"

namespace android {
namespace hardware {
namespace nfc {
namespace V1_0 {
namespace implementation {

namespace {

/*
 * The Samsung NFC HAL module never reports HAL_NFC_OPEN_CPLT_EVT. It resets
 * the controller itself while opening and passes the CORE_RESET_RSP up
 * instead. libnfc-nci tolerates that during a normal enable, but waits for
 * HAL_NFC_OPEN_CPLT_EVT forever when NfcService checks the firmware at boot
 * with NFC turned off.
 *
 * Report that response as the completion of open() and hand it back when
 * the stack sends the CORE_RESET_CMD that follows HAL_NFC_OPEN_CPLT_EVT, so
 * that the module still sees exactly the sequence it expects.
 */
constexpr uint8_t kNciMtMask = 0xE0;
constexpr uint8_t kNciMtCmd = 0x20;
constexpr uint8_t kNciMtRsp = 0x40;
constexpr uint8_t kNciGidMask = 0x0F;
constexpr uint8_t kNciOidMask = 0x3F;
constexpr uint8_t kNciGidCore = 0x00;
constexpr uint8_t kNciOidCoreReset = 0x00;
constexpr uint16_t kNciHdrSize = 3;

bool isCoreReset(const uint8_t* data, size_t len, uint8_t mt) {
    return data != nullptr && len >= kNciHdrSize && (data[0] & kNciMtMask) == mt &&
           (data[0] & kNciGidMask) == kNciGidCore && (data[1] & kNciOidMask) == kNciOidCoreReset;
}

}  // namespace

sp<INfcClientCallback> Nfc::mCallback = nullptr;
std::mutex Nfc::mLock;
bool Nfc::mOpening = false;
bool Nfc::mOpenReported = false;
bool Nfc::mDeviceOpen = false;
bool Nfc::mClosing = false;
uint64_t Nfc::mGeneration = 0;
std::vector<uint8_t> Nfc::mResetRsp;

Nfc::Nfc(nfc_nci_device_t* device) : mDevice(device) {}

// Methods from ::android::hardware::nfc::V1_0::INfc follow.
Return<NfcStatus> Nfc::open(const sp<INfcClientCallback>& clientCallback) {
    if (mDevice == nullptr || clientCallback == nullptr) {
        return NfcStatus::FAILED;
    }
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(mLock);
        // The legacy module has one global transport. Calling open twice resets
        // its state machine, even if it is in the middle of a firmware update.
        if (mDeviceOpen || mClosing) {
            ALOGW("Rejecting overlapping NFC open");
            return NfcStatus::FAILED;
        }
        mDeviceOpen = true;
        generation = ++mGeneration;
        mCallback = clientCallback;
        mOpening = true;
        mOpenReported = false;
        mResetRsp.clear();
    }
    clientCallback->linkToDeath(this, generation);
    int ret = mDevice->open(mDevice, eventCallback, dataCallback);
    if (ret != 0) {
        clientCallback->unlinkToDeath(this);
        std::lock_guard<std::mutex> lock(mLock);
        mDeviceOpen = false;
        mOpening = false;
        mCallback = nullptr;
        mResetRsp.clear();
    }
    return ret == 0 ? NfcStatus::OK : NfcStatus::FAILED;
}

Return<uint32_t> Nfc::write(const hidl_vec<uint8_t>& data) {
    if (mDevice == nullptr || data.size() == 0 || data.size() > UINT16_MAX) {
        return 0;
    }

    sp<INfcClientCallback> callback;
    std::vector<uint8_t> rsp;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (!mDeviceOpen || mClosing) return 0;
        if (!mResetRsp.empty()) {
            if (isCoreReset(data.data(), data.size(), kNciMtCmd)) {
                callback = mCallback;
                rsp.swap(mResetRsp);
            } else {
                mResetRsp.clear();
            }
        }
    }
    if (callback != nullptr) {
        ALOGD("Replaying CORE_RESET_RSP of the open sequence");
        callback->sendData(rsp);
        return data.size();
    }

    // The legacy ABI returns a signed error, whereas HIDL returns a byte
    // count. Never turn an I/O error into a huge successful transfer.
    int written = mDevice->write(mDevice, data.size(), &data[0]);
    return written > 0 && static_cast<size_t>(written) <= data.size() ? written : 0;
}

Return<NfcStatus> Nfc::coreInitialized(const hidl_vec<uint8_t>& data) {
    hidl_vec<uint8_t> copy = data;

    if (mDevice == nullptr || copy.size() == 0) {
        return NfcStatus::FAILED;
    }
    int ret = mDevice->core_initialized(mDevice, &copy[0]);
    return ret == 0 ? NfcStatus::OK : NfcStatus::FAILED;
}

Return<NfcStatus> Nfc::prediscover() {
    if (mDevice == nullptr) {
        return NfcStatus::FAILED;
    }
    return mDevice->pre_discover(mDevice) ? NfcStatus::FAILED : NfcStatus::OK;
}

Return<NfcStatus> Nfc::close() {
    return closeInternal(0);
}

Return<NfcStatus> Nfc::closeInternal(uint64_t generation) {
    sp<INfcClientCallback> callback;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (!mDeviceOpen || mClosing || (generation != 0 && generation != mGeneration)) {
            return NfcStatus::FAILED;
        }
        mClosing = true;
        callback = mCallback;
        mOpening = false;
        mResetRsp.clear();
    }
    if (mDevice == nullptr || callback == nullptr) {
        return NfcStatus::FAILED;
    }
    callback->unlinkToDeath(this);
    int ret = mDevice->close(mDevice);
    {
        std::lock_guard<std::mutex> lock(mLock);
        mCallback = nullptr;
        mDeviceOpen = false;
        mClosing = false;
        mOpenReported = false;
    }
    return ret ? NfcStatus::FAILED : NfcStatus::OK;
}

Return<NfcStatus> Nfc::controlGranted() {
    if (mDevice == nullptr) {
        return NfcStatus::FAILED;
    }
    return mDevice->control_granted(mDevice) ? NfcStatus::FAILED : NfcStatus::OK;
}

Return<NfcStatus> Nfc::powerCycle() {
    if (mDevice == nullptr) {
        return NfcStatus::FAILED;
    }
    return mDevice->power_cycle(mDevice) ? NfcStatus::FAILED : NfcStatus::OK;
}

void Nfc::eventCallback(uint8_t event, uint8_t status) {
    sp<INfcClientCallback> callback;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (event == HAL_NFC_OPEN_CPLT_EVT) {
            if (mOpenReported) {
                return;
            }
            mOpening = false;
            mOpenReported = true;
        }
        callback = mCallback;
    }
    if (callback != nullptr) {
        auto ret = callback->sendEvent((NfcEvent)event, (NfcStatus)status);
        if (!ret.isOk()) {
            ALOGW("Failed to call back into NFC process.");
        }
    }
}

void Nfc::dataCallback(uint16_t data_len, uint8_t* p_data) {
    sp<INfcClientCallback> callback;
    bool reportOpen = false;
    NfcStatus status = NfcStatus::OK;
    {
        std::lock_guard<std::mutex> lock(mLock);
        callback = mCallback;
        if (mOpening && !mOpenReported && isCoreReset(p_data, data_len, kNciMtRsp)) {
            mResetRsp.assign(p_data, p_data + data_len);
            mOpening = false;
            mOpenReported = true;
            reportOpen = true;
            /* the first payload byte of the response is its status */
            if (data_len <= kNciHdrSize || p_data[kNciHdrSize] != 0) {
                status = NfcStatus::FAILED;
            }
        }
    }
    if (callback == nullptr) {
        return;
    }
    if (reportOpen) {
        ALOGD("CORE_RESET_RSP during open, reporting HAL_NFC_OPEN_CPLT_EVT");
        callback->sendEvent(NfcEvent::OPEN_CPLT, status);
        return;
    }

    ::android::hardware::hidl_vec<uint8_t> data;
    data.setToExternal(p_data, data_len);
    auto ret = callback->sendData(data);
    if (!ret.isOk()) {
        ALOGW("Failed to call back into NFC process.");
    }
}

void Nfc::serviceDied(uint64_t cookie, const wp<hidl::base::V1_0::IBase>& /*who*/) {
    // Test the generation and claim the close in one critical section, so a
    // stale owner cannot race with reopening by a newer NFC process.
    closeInternal(cookie);
}

Nfc* Nfc::create() {
    nfc_nci_device_t* nfc_device;
    const hw_module_t* hw_module = nullptr;

    int ret = hw_get_module(NFC_NCI_HARDWARE_MODULE_ID, &hw_module);
    if (ret != 0) {
        ALOGE("hw_get_module %s failed: %d", NFC_NCI_HARDWARE_MODULE_ID, ret);
        return nullptr;
    }
    ret = nfc_nci_open(hw_module, &nfc_device);
    if (ret != 0) {
        ALOGE("nfc_nci_open failed: %d", ret);
        return nullptr;
    }
    return new Nfc(nfc_device);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace nfc
}  // namespace hardware
}  // namespace android
