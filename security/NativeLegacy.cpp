// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "a3tee-native"
#include "NativeLegacy.h"
#include <keymaster/legacy_support/rsa_keymaster0_key.h>
#include <keymaster/key_blob_utils/integrity_assured_key_blob.h>
#include <keymaster/key_blob_utils/software_keyblobs.h>
#include <log/log.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <dlfcn.h>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace a3tee {
namespace {
// The old Samsung binary writes RSA struct fields using its Android 5 OpenSSL
// ABI. That cannot construct a public key with Android 12 BoringSSL. Read only
// the public components from the fixed Qualcomm KM0 blob and use current APIs.
int publicKey(const keymaster0_device_t*, const uint8_t* blob, size_t length,
              uint8_t** output, size_t* outputLength) {
    if (!output || !outputLength) return -1;
    *output=nullptr; *outputLength=0;
    if (!blob || length!=1604) return -1;
    auto word=[&](size_t pos) { uint32_t v; memcpy(&v,blob+pos,4); return v; };
    uint32_t modulus=word(520), exponent=word(1036);
    if (word(0)!=0x4b4d4b42 || !modulus || modulus>512 || !exponent || exponent>512) return -1;
    bssl::UniquePtr<RSA> rsa(RSA_new());
    bssl::UniquePtr<BIGNUM> n(BN_bin2bn(blob+8,modulus,nullptr)), e(BN_bin2bn(blob+524,exponent,nullptr));
    if (!rsa || !n || !e || !RSA_set0_key(rsa.get(),n.get(),e.get(),nullptr)) return -1;
    n.release(); e.release();
    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
    if (!key || !EVP_PKEY_set1_RSA(key.get(),rsa.get())) return -1;
    int size=i2d_PUBKEY(key.get(),nullptr);
    if (size<=0) return -1;
    auto* bytes=static_cast<uint8_t*>(malloc(size));
    if (!bytes) return -1;
    auto* ptr=bytes;
    if (i2d_PUBKEY(key.get(),&ptr)!=size) { free(bytes); return -1; }
    *output=bytes; *outputLength=size; return 0;
}
constexpr uint8_t kSelfTestKey[] = {
#include "SelfTestKey.inc"
};
// Proves the module drives a working secure-world RSA implementation instead of
// trusting a firmware or bootloader version: a raw private operation in the TA
// must invert under the public key. Import of a fixed key is fast; generation
// (several seconds in a KM0 TA) is only the fallback when a TA rejects import.
bool selfTest(keymaster0_device_t* device) {
    const auto start = std::chrono::steady_clock::now();
    uint8_t *key = nullptr, *pub = nullptr, *signature = nullptr;
    size_t keySize = 0, pubSize = 0, signatureSize = 0;
    bool imported =
        device->import_keypair(device, kSelfTestKey, sizeof(kSelfTestKey), &key, &keySize) == 0 && key;
    if (!imported) {
        keymaster_rsa_keygen_params_t params{2048, 65537};
        if (device->generate_keypair(device, TYPE_RSA, &params, &key, &keySize) || !key) {
            ALOGE("Native Keymaster self-test: no RSA key could be created");
            return false;
        }
    }
    bool ok = false;
    if (device->get_keypair_public(device, key, keySize, &pub, &pubSize) == 0 && pub) {
        const uint8_t* ptr = pub;
        bssl::UniquePtr<EVP_PKEY> pk(d2i_PUBKEY(nullptr, &ptr, pubSize));
        bssl::UniquePtr<RSA> rsa(pk ? EVP_PKEY_get1_RSA(pk.get()) : nullptr);
        if (imported && rsa) {
            const uint8_t* expected = kSelfTestKey;
            bssl::UniquePtr<EVP_PKEY> reference(d2i_AutoPrivateKey(nullptr, &expected, sizeof(kSelfTestKey)));
            bssl::UniquePtr<RSA> ref(reference ? EVP_PKEY_get1_RSA(reference.get()) : nullptr);
            if (!ref || BN_cmp(RSA_get0_n(ref.get()), RSA_get0_n(rsa.get())) != 0) rsa.reset();
        }
        uint8_t input[256]{};
        input[1] = 0x5a; input[255] = 0x42; // below the modulus for any 2048-bit key
        keymaster_rsa_sign_params_t sign{DIGEST_NONE, PADDING_NONE};
        uint8_t recovered[256];
        ok = rsa && RSA_size(rsa.get()) == sizeof(input) &&
            device->sign_data(device, &sign, key, keySize, input, sizeof(input), &signature, &signatureSize) == 0 &&
            signatureSize == sizeof(input) &&
            RSA_public_decrypt(signatureSize, signature, recovered, rsa.get(), RSA_NO_PADDING) == int(sizeof(input)) &&
            memcmp(input, recovered, sizeof(input)) == 0;
    }
    if (device->delete_keypair) device->delete_keypair(device, key, keySize);
    OPENSSL_cleanse(key, keySize);
    free(key); free(pub); free(signature);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    ALOGI("Native Keymaster self-test %s (%s key, %lld ms)", ok ? "passed" : "failed",
          imported ? "imported" : "generated", static_cast<long long>(ms));
    return ok;
}
} // namespace
keymaster0_device_t* openNativeLegacy() {
    // NODELETE keeps the module's method table alive until the engine closes.
    void* library=dlopen("/vendor/lib/keystore.qcom.so",RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);
    if (!library) { ALOGE("Native module load: %s",dlerror()); return nullptr; }
    auto* module=static_cast<hw_module_t*>(dlsym(library,HAL_MODULE_INFO_SYM_AS_STR));
    keymaster0_device_t* device=nullptr;
    bool valid=module && module->methods && module->methods->open &&
        module->module_api_version<=KEYMASTER_MODULE_API_VERSION_0_3 &&
        keymaster0_open(module,&device)==0 && device;
    dlclose(library);
    if (!valid) return nullptr;
    if ((device->flags&KEYMASTER_SOFTWARE_ONLY) || !device->generate_keypair || !device->sign_data ||
        !device->import_keypair || !device->common.close) {
        if (device->common.close) keymaster0_close(device);
        return nullptr;
    }
    device->get_keypair_public=publicKey;
    if (!selfTest(device)) { keymaster0_close(device); return nullptr; }
    return device;
}
NativeLegacyContext::NativeLegacyContext(keymaster0_device_t* device)
    : PureSoftKeymasterContext(keymaster::KmVersion::KEYMASTER_3), engine_(device),
      rsa_(new keymaster::RsaKeymaster0KeyFactory(*this,*this,&engine_)) {}
keymaster::KeyFactory* NativeLegacyContext::GetKeyFactory(keymaster_algorithm_t algorithm) const {
    return algorithm==KM_ALGORITHM_RSA ? rsa_.get() : PureSoftKeymasterContext::GetKeyFactory(algorithm);
}
keymaster_error_t NativeLegacyContext::CreateKeyBlob(const keymaster::AuthorizationSet& description,
    keymaster_key_origin_t origin, const keymaster::KeymasterKeyBlob& material,
    keymaster::KeymasterKeyBlob* blob, keymaster::AuthorizationSet* hw,
    keymaster::AuthorizationSet* sw) const {
    auto error=PureSoftKeymasterContext::CreateKeyBlob(description,origin,material,blob,hw,sw);
    if (error) return error;
    // AOSP's software blob maker puts OS metadata in any nonempty HW list.
    // KM0 does not bind keys to OS/patch versions; never advertise that policy.
    keymaster::AuthorizationSet actual;
    for (auto& p:*hw) {
        if (p.tag==KM_TAG_ALGORITHM || p.tag==KM_TAG_KEY_SIZE || p.tag==KM_TAG_RSA_PUBLIC_EXPONENT ||
            p.tag==KM_TAG_ORIGIN)
            actual.push_back(p);
        else sw->push_back(p);
    }
    *hw=std::move(actual);
    keymaster::AuthorizationSet hidden;
    error=keymaster::BuildHiddenAuthorizations(description,&hidden,keymaster::softwareRootOfTrust);
    return error ? error : keymaster::SerializeIntegrityAssuredBlob(material,hidden,*hw,*sw,blob);
}
keymaster::CertificateChain NativeLegacyContext::GenerateAttestation(const keymaster::Key&,
    const keymaster::AuthorizationSet&, keymaster::UniquePtr<keymaster::Key>,
    const keymaster::KeymasterBlob&, keymaster_error_t* error) const {
    *error=KM_ERROR_UNIMPLEMENTED; return {};
}
} // namespace a3tee
