// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#define LOG_TAG "a3tee-keymaster"
#include "Keymaster.h"
#include "Csg3Context.h"
#include "NativeLegacy.h"
#include <AndroidKeymaster3Device.h>
#include <keymaster/contexts/keymaster1_passthrough_context.h>
#include <keymaster/keymaster_configuration.h>
#include <hidl/HidlTransportSupport.h>
#include <android-base/properties.h>
#include <log/log.h>
#include <cstdlib>

namespace {
using namespace android::hardware;
using namespace android::hardware::keymaster::V3_0;
class Csg3Keymaster final : public ::keymaster::ng::AndroidKeymaster3Device {
  public:
    explicit Csg3Keymaster(::keymaster::KeymasterContext* context, bool legacy = false)
        : AndroidKeymaster3Device(context, legacy ? ::keymaster::ng::KeymasterHardwareProfile::KM0
                                                 : ::keymaster::ng::KeymasterHardwareProfile::KM1),
          legacy_(legacy) {}
    Return<void> getHardwareFeatures(getHardwareFeatures_cb cb) override {
        cb(true, !legacy_, !legacy_, false, false,
           legacy_ ? "Samsung native Keymaster 0.3" : "Samsung CSG3 Keymaster 1.0", "Samsung/QTI");
        return Void();
    }
    Return<void> attestKey(const hidl_vec<uint8_t>&, const hidl_vec<KeyParameter>&,
                           attestKey_cb cb) override {
        // The generic adapter also contains software attestation. This service
        // must not manufacture an attestation for an old TA that has none.
        cb(ErrorCode::UNIMPLEMENTED, {});
        return Void();
    }
    Return<ErrorCode> destroyAttestationIds() override { return ErrorCode::UNIMPLEMENTED; }
  private:
    bool legacy_;
};
}
int main() {
    if (!android::base::GetBoolProperty("ro.vendor.a3tee.enabled", false)) {
        ALOGE("Hardware security is not selected for this build"); return 1;
    }
    if (android::base::GetProperty("ro.bootloader", "") == "A300FXXS1BRH1") {
        auto* device = a3tee::openNativeLegacy();
        if (!device) return 1;
        auto* context = new a3tee::NativeLegacyContext(device);
        context->SetSystemVersion(::keymaster::GetOsVersion(), ::keymaster::GetOsPatchlevel());
        android::hardware::configureRpcThreadpool(1, true);
        android::sp<IKeymasterDevice> service = new Csg3Keymaster(context, true);
        if (service->registerAsService("default") != android::OK) return 1;
        android::hardware::joinRpcThreadpool();
        return 1;
    }
    std::unique_ptr<a3tee::Transport> transport(a3tee::openQsee());
    if (!transport) { ALOGE("Qualified hardware Keymaster unavailable; refusing software fallback"); return 1; }
    keymaster1_device_t* device = a3tee::makeKeymaster(nullptr, std::move(transport));
    if (!device) return 1;
    keymaster_algorithm_t* algorithms = nullptr;
    size_t count = 0;
    bool aes = false, hmac = false, rsa = false, ec = false;
    auto error = device->get_supported_algorithms(device, &algorithms, &count);
    if (!error) for (size_t i = 0; i < count; ++i) {
        aes |= algorithms[i] == KM_ALGORITHM_AES;
        hmac |= algorithms[i] == KM_ALGORITHM_HMAC;
        rsa |= algorithms[i] == KM_ALGORITHM_RSA;
        ec |= algorithms[i] == KM_ALGORITHM_EC;
    }
    free(algorithms);
    if (error || !aes || !hmac || !rsa || !ec) {
        ALOGE("CSG3 mandatory algorithms unavailable");
        device->common.close(&device->common);
        return 1;
    }
    // AOSP's KM1 passthrough supplies the HIDL3 API. Keystore2's existing
    // km_compat bridges HIDL3 to the Android12 KeyMint-facing interface.
    // Do not claim hardware attestation, KM4.1 or KeyMint enforcement.
    android::hardware::configureRpcThreadpool(1, true);
    auto* context = new a3tee::Csg3Context(device);
    context->SetSystemVersion(::keymaster::GetOsVersion(), ::keymaster::GetOsPatchlevel());
    android::sp<android::hardware::keymaster::V3_0::IKeymasterDevice> service = new Csg3Keymaster(context);
    if (!service || service->registerAsService("default") != android::OK) return 1;
    android::hardware::joinRpcThreadpool();
    return 1;
}
