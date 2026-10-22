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
#include <log/log.h>
#include <cstdlib>
#include <iterator>
#include <vector>

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

// Functional check of a Keymaster 1 TA: the mandatory algorithms are listed and
// a temporary HMAC key really signs, verifies and rejects a damaged MAC. This,
// not the TA's version number, decides whether the backend is used.
bool km1SelfTest(keymaster1_device_t* km) {
    keymaster_algorithm_t* algorithms = nullptr;
    size_t count = 0;
    bool aes = false, hmac = false, rsa = false, ec = false;
    auto error = km->get_supported_algorithms(km, &algorithms, &count);
    if (!error) for (size_t i = 0; i < count; ++i) {
        aes |= algorithms[i] == KM_ALGORITHM_AES;
        hmac |= algorithms[i] == KM_ALGORITHM_HMAC;
        rsa |= algorithms[i] == KM_ALGORITHM_RSA;
        ec |= algorithms[i] == KM_ALGORITHM_EC;
    }
    free(algorithms);
    if (error || !aes || !hmac || !rsa || !ec) {
        ALOGE("Keymaster 1 TA lacks mandatory algorithms");
        return false;
    }
    keymaster_key_param_t keyParams[] = {
        keymaster_param_enum(KM_TAG_ALGORITHM, KM_ALGORITHM_HMAC),
        keymaster_param_int(KM_TAG_KEY_SIZE, 256),
        keymaster_param_enum(KM_TAG_DIGEST, KM_DIGEST_SHA_2_256),
        keymaster_param_int(KM_TAG_MIN_MAC_LENGTH, 256),
        keymaster_param_enum(KM_TAG_PURPOSE, KM_PURPOSE_SIGN),
        keymaster_param_enum(KM_TAG_PURPOSE, KM_PURPOSE_VERIFY),
        keymaster_param_bool(KM_TAG_NO_AUTH_REQUIRED),
    };
    keymaster_key_param_set_t keySet{keyParams, std::size(keyParams)};
    keymaster_key_blob_t key{};
    keymaster_key_characteristics_t* characteristics = nullptr;
    if (km->generate_key(km, &keySet, &key, &characteristics)) return false;
    if (characteristics) { keymaster_free_characteristics(characteristics); free(characteristics); }
    static const uint8_t kMessage[] = "a3tee keymaster self-test";
    auto run = [&](keymaster_purpose_t purpose, std::vector<keymaster_key_param_t> params,
                   const keymaster_blob_t* signature, std::vector<uint8_t>* output) {
        keymaster_key_param_set_t in{params.data(), params.size()}, out{};
        keymaster_operation_handle_t op = 0;
        auto e = km->begin(km, purpose, &key, &in, &out, &op);
        keymaster_free_param_set(&out);
        if (e) return e;
        size_t pos = 0;
        while (pos < sizeof(kMessage)) {
            keymaster_blob_t input{kMessage + pos, sizeof(kMessage) - pos}, produced{};
            size_t consumed = 0;
            e = km->update(km, op, nullptr, &input, &consumed, &out, &produced);
            keymaster_free_param_set(&out);
            free(const_cast<uint8_t*>(produced.data));
            if (e || !consumed) { km->abort(km, op); return e ? e : KM_ERROR_UNKNOWN_ERROR; }
            pos += consumed;
        }
        keymaster_blob_t produced{};
        e = km->finish(km, op, nullptr, signature, &out, &produced);
        keymaster_free_param_set(&out);
        if (!e && output) output->assign(produced.data, produced.data + produced.data_length);
        free(const_cast<uint8_t*>(produced.data));
        return e;
    };
    std::vector<uint8_t> mac;
    bool ok = run(KM_PURPOSE_SIGN, {keymaster_param_int(KM_TAG_MAC_LENGTH, 256)}, nullptr, &mac) == KM_ERROR_OK &&
        mac.size() == 32;
    if (ok) {
        keymaster_blob_t good{mac.data(), mac.size()};
        ok = run(KM_PURPOSE_VERIFY, {}, &good, nullptr) == KM_ERROR_OK;
        mac[0] ^= 1;
        keymaster_blob_t bad{mac.data(), mac.size()};
        ok = ok && run(KM_PURPOSE_VERIFY, {}, &bad, nullptr) == KM_ERROR_VERIFICATION_FAILED;
    }
    if (km->delete_key) km->delete_key(km, &key);
    free(const_cast<uint8_t*>(key.key_material));
    ALOGI("Keymaster 1 TA self-test %s", ok ? "passed" : "failed");
    return ok;
}

android::sp<IKeymasterDevice> openNativeKm0() {
    auto* device = a3tee::openNativeLegacy();
    if (!device) return nullptr;
    auto* context = new a3tee::NativeLegacyContext(device);
    context->SetSystemVersion(::keymaster::GetOsVersion(), ::keymaster::GetOsPatchlevel());
    return new Csg3Keymaster(context, true);
}

android::sp<IKeymasterDevice> openKm1() {
    std::unique_ptr<a3tee::Transport> transport(a3tee::openQsee());
    if (!transport) return nullptr;
    keymaster1_device_t* device = a3tee::makeKeymaster(nullptr, std::move(transport));
    if (!device) return nullptr;
    if (!km1SelfTest(device)) { device->common.close(&device->common); return nullptr; }
    // AOSP's KM1 passthrough supplies the HIDL3 API. Keystore2's existing
    // km_compat bridges HIDL3 to the Android12 KeyMint-facing interface.
    // Do not claim hardware attestation, KM4.1 or KeyMint enforcement.
    auto* context = new a3tee::Csg3Context(device);
    context->SetSystemVersion(::keymaster::GetOsVersion(), ::keymaster::GetOsPatchlevel());
    return new Csg3Keymaster(context);
}
}
int main() {
    // Backends are chosen by what the secure world demonstrably does, in order
    // of preference, never by bootloader or firmware identity. If no TA passes
    // its self-test the device keeps a working keystore with the same software
    // Keymaster used before this HAL; TEE-bound RSA blobs stay unusable then.
    android::sp<IKeymasterDevice> service = openNativeKm0();
    const char* backend = "native Keymaster 0.3 TA";
    if (!service) { service = openKm1(); backend = "Keymaster 1 TA"; }
    if (!service) {
        service = new ::keymaster::ng::AndroidKeymaster3Device();
        backend = "software Keymaster (no working TA)";
    }
    ALOGI("Using %s", backend);
    android::hardware::configureRpcThreadpool(1, true);
    if (service->registerAsService("default") != android::OK) return 1;
    android::hardware::joinRpcThreadpool();
    return 1;
}
