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

#pragma once

#include <mutex>
#include <vector>

#include <android/hardware/nfc/1.0/INfc.h>
#include <android/hardware/nfc/1.0/INfcClientCallback.h>
#include <hardware/nfc.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace nfc {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::wp;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;

struct Nfc : public INfc, public hidl_death_recipient {
    Nfc(nfc_nci_device_t* device);
    static Nfc* create();

    // Methods from ::android::hardware::nfc::V1_0::INfc follow.
    Return<NfcStatus> open(const sp<INfcClientCallback>& clientCallback) override;
    Return<uint32_t> write(const hidl_vec<uint8_t>& data) override;
    Return<NfcStatus> coreInitialized(const hidl_vec<uint8_t>& data) override;
    Return<NfcStatus> prediscover() override;
    Return<NfcStatus> close() override;
    Return<NfcStatus> controlGranted() override;
    Return<NfcStatus> powerCycle() override;

    static void eventCallback(uint8_t event, uint8_t status);
    static void dataCallback(uint16_t data_len, uint8_t* p_data);

    void serviceDied(uint64_t cookie, const wp<hidl::base::V1_0::IBase>& who) override;

  private:
    static sp<INfcClientCallback> mCallback;
    static std::mutex mLock;
    static bool mOpening;
    static bool mOpenReported;
    static std::vector<uint8_t> mResetRsp;
    const nfc_nci_device_t* mDevice;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace nfc
}  // namespace hardware
}  // namespace android
