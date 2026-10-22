// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#define LOG_TAG "a3tee-gatekeeper"
#include "Gatekeeper.h"
#include <android/hardware/gatekeeper/1.0/IGatekeeper.h>
#include <hidl/HidlTransportSupport.h>
#include <android-base/properties.h>
#include <log/log.h>
#include <cstdlib>
#include <memory>

using namespace android::hardware;
using namespace android::hardware::gatekeeper::V1_0;
namespace {
keymaster_blob_t blob(const hidl_vec<uint8_t>& v) { return {v.data(), v.size()}; }
GatekeeperResponse response(int status, keymaster_blob_t data = {}) {
    GatekeeperResponse out{};
    if (status > 0) {
        out.code = GatekeeperStatusCode::ERROR_RETRY_TIMEOUT;
        out.timeout = status;
    } else if (status) out.code = GatekeeperStatusCode::ERROR_GENERAL_FAILURE;
    else {
        out.code = GatekeeperStatusCode::STATUS_OK;
        out.data.resize(data.data_length);
        if (data.data_length) memcpy(out.data.data(), data.data, data.data_length);
    }
    a3tee::wipe(const_cast<uint8_t*>(data.data), data.data_length);
    free(const_cast<uint8_t*>(data.data));
    return out;
}
class HardwareGatekeeper final : public IGatekeeper {
  public:
    explicit HardwareGatekeeper(std::unique_ptr<a3tee::Transport> transport)
        : transport_(std::move(transport)), backend_(*transport_) {}
    Return<void> enroll(uint32_t uid, const hidl_vec<uint8_t>& handle,
                         const hidl_vec<uint8_t>& current, const hidl_vec<uint8_t>& desired,
                         enroll_cb cb) override {
        keymaster_blob_t out{};
        int status = backend_.enroll(uid, blob(handle), blob(current), blob(desired), &out);
        cb(response(status, out)); return Void();
    }
    Return<void> verify(uint32_t uid, uint64_t challenge, const hidl_vec<uint8_t>& handle,
                         const hidl_vec<uint8_t>& password, verify_cb cb) override {
        keymaster_blob_t out{};
        int status = backend_.verify(uid, challenge, blob(handle), blob(password), &out);
        cb(response(status, out)); return Void();
    }
    Return<void> deleteUser(uint32_t uid, deleteUser_cb cb) override {
        cb(response(backend_.deleteUser(uid))); return Void();
    }
    Return<void> deleteAllUsers(deleteAllUsers_cb cb) override {
        cb(response(backend_.deleteAllUsers())); return Void();
    }
  private:
    std::unique_ptr<a3tee::Transport> transport_;
    a3tee::Gatekeeper backend_;
};
}
int main() {
    if (!android::base::GetBoolProperty("ro.vendor.a3tee.enabled", false)) {
        ALOGE("Hardware security is not selected for this build"); return 1;
    }
    std::unique_ptr<a3tee::Transport> transport(a3tee::openQsee());
    if (!transport) { ALOGE("Qualified hardware Gatekeeper unavailable; refusing software fallback"); return 1; }
    configureRpcThreadpool(1, true);
    android::sp<IGatekeeper> service = new HardwareGatekeeper(std::move(transport));
    if (service->registerAsService("default") != android::OK) return 1;
    joinRpcThreadpool();
    return 1;
}
