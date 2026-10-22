// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "a3tee-native"
#include "NativeLegacy.h"
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <keymaster/legacy_support/rsa_keymaster0_key.h>
#include <keymaster/key_blob_utils/integrity_assured_key_blob.h>
#include <keymaster/key_blob_utils/software_keyblobs.h>
#include <log/log.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <dlfcn.h>
#include <cstring>

namespace a3tee {
namespace {
struct Hash { const char* name; const char* sha256; };
constexpr Hash kNative[] = {
#include "NativeFirmwareHashes.inc"
};
bool qualified() {
    if (android::base::GetProperty("ro.bootloader", "") != "A300FXXS1BRH1") return false;
    std::string override;
    if (!android::base::ReadFileToString("/sys/module/firmware_class/parameters/path", &override) ||
        !android::base::Trim(override).empty()) return false;
    for (auto& expected : kNative) {
        std::string data;
        if (!android::base::ReadFileToString(std::string("/firmware/image/") + expected.name, &data)) return false;
        uint8_t hash[32]; SHA256(reinterpret_cast<const uint8_t*>(data.data()), data.size(), hash);
        char hex[65];
        for (size_t i=0; i<32; ++i) snprintf(hex+2*i, 3, "%02x", hash[i]);
        if (strcmp(hex, expected.sha256)) return false;
    }
    return true;
}
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
} // namespace
keymaster0_device_t* openNativeLegacy() {
    if (!qualified()) { ALOGE("Native A300F firmware qualification failed"); return nullptr; }
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
