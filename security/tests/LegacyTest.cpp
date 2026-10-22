// SPDX-License-Identifier: Apache-2.0
// Temporary, unauthenticated RSA keys only. No enrollment or delete-all calls.
#include <hardware/keymaster0.h>
#include <dlfcn.h>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <cstdlib>
#include <cstring>
#include "NativeLegacy.h"
#include <AndroidKeymaster3Device.h>
#include <keymaster/keymaster_configuration.h>

TEST(NativeLegacy, RsaPrivateOperation) {
    keymaster0_device_t* device = a3tee::openNativeLegacy();
    ASSERT_NE(device, nullptr);
    printf("device flags=0x%x version=0x%x\n", device->flags, device->common.version);
    ASSERT_FALSE(device->flags & KEYMASTER_SOFTWARE_ONLY);
    keymaster_rsa_keygen_params_t params{2048, 65537};
    uint8_t *key = nullptr, *pub = nullptr, *signature = nullptr;
    size_t keySize = 0, pubSize = 0, signatureSize = 0;
    ASSERT_EQ(device->generate_keypair(device, TYPE_RSA, &params, &key, &keySize), 0);
    ASSERT_NE(key, nullptr);
    printf("opaque key bytes=%zu\n", keySize);
    ASSERT_EQ(device->get_keypair_public(device, key, keySize, &pub, &pubSize), 0);
    const uint8_t* ptr = pub;
    bssl::UniquePtr<EVP_PKEY> pk(d2i_PUBKEY(nullptr, &ptr, pubSize));
    ASSERT_NE(pk, nullptr);
    bssl::UniquePtr<RSA> rsa(EVP_PKEY_get1_RSA(pk.get()));
    ASSERT_NE(rsa, nullptr);
    uint8_t input[256]{}; input[255] = 0x42;
    keymaster_rsa_sign_params_t sign{DIGEST_NONE, PADDING_NONE};
    ASSERT_EQ(device->sign_data(device, &sign, key, keySize, input, sizeof(input),
                               &signature, &signatureSize), 0);
    ASSERT_EQ(signatureSize, sizeof(input));
    uint8_t recovered[256];
    ASSERT_EQ(RSA_public_decrypt(signatureSize, signature, recovered, rsa.get(), RSA_NO_PADDING), 256);
    EXPECT_EQ(memcmp(input, recovered, sizeof(input)), 0);
    OPENSSL_cleanse(key, keySize); free(key); free(pub); free(signature);
    EXPECT_EQ(keymaster0_close(device), 0);
}

TEST(NativeModern, HidlRsaSha256AndApplicationBinding) {
    using namespace android::hardware;
    using namespace android::hardware::keymaster::V3_0;
    auto* device = a3tee::openNativeLegacy();
    ASSERT_NE(device, nullptr);
    auto* context = new a3tee::NativeLegacyContext(device);
    context->SetSystemVersion(::keymaster::GetOsVersion(), ::keymaster::GetOsPatchlevel());
    android::sp<IKeymasterDevice> hal = new ::keymaster::ng::AndroidKeymaster3Device(
        context, ::keymaster::ng::KeymasterHardwareProfile::KM0);
    auto number=[](Tag tag, uint32_t n) { KeyParameter p{};p.tag=tag;p.f.integer=n;return p; };
    auto enumerated=[](Tag tag, uint32_t n) { KeyParameter p{};p.tag=tag;p.f.algorithm=Algorithm(n);return p; };
    KeyParameter exponent{};exponent.tag=Tag::RSA_PUBLIC_EXPONENT;exponent.f.longInteger=65537;
    KeyParameter noAuth{};noAuth.tag=Tag::NO_AUTH_REQUIRED;noAuth.f.boolValue=true;
    KeyParameter app{};app.tag=Tag::APPLICATION_ID;app.blob={0x12,0x34,0x56};
    hidl_vec<KeyParameter> params={enumerated(Tag::ALGORITHM,uint32_t(Algorithm::RSA)),
        number(Tag::KEY_SIZE,2048),exponent,enumerated(Tag::PURPOSE,uint32_t(KeyPurpose::SIGN)),
        enumerated(Tag::DIGEST,uint32_t(Digest::SHA_2_256)),
        enumerated(Tag::PADDING,uint32_t(PaddingMode::RSA_PKCS1_1_5_SIGN)),noAuth,app};
    hidl_vec<uint8_t> blob, pub, signature;
    ASSERT_TRUE(hal->generateKey(params,[&](ErrorCode e,const auto& key,const auto& chars) {
        ASSERT_EQ(e,ErrorCode::OK);blob=key;
        bool algorithm=false;
        for(auto& p:chars.teeEnforced) {
            algorithm|=p.tag==Tag::ALGORITHM;
            EXPECT_NE(p.tag,Tag::USER_AUTH_TYPE);EXPECT_NE(p.tag,Tag::OS_PATCHLEVEL);
        }
        EXPECT_TRUE(algorithm);
    }).isOk());
    ASSERT_GT(blob.size(),0u);
    auto tampered=blob;tampered[tampered.size()-1]^=1;
    hal->exportKey(KeyFormat::X509,tampered,app.blob,{},[&](ErrorCode e,const auto&) {
        EXPECT_EQ(e,ErrorCode::INVALID_KEY_BLOB);
    });
    hal->exportKey(KeyFormat::X509,blob,app.blob,{},[&](ErrorCode e,const auto& out) {
        ASSERT_EQ(e,ErrorCode::OK);pub=out;
    });
    ASSERT_GT(pub.size(),0u);
    hal->exportKey(KeyFormat::X509,blob,{0xff},{},[&](ErrorCode e,const auto&) {
        EXPECT_EQ(e,ErrorCode::INVALID_KEY_BLOB);
    });
    hidl_vec<KeyParameter> operation={enumerated(Tag::DIGEST,uint32_t(Digest::SHA_2_256)),
        enumerated(Tag::PADDING,uint32_t(PaddingMode::RSA_PKCS1_1_5_SIGN)),app};
    uint64_t handle=0;
    hal->begin(KeyPurpose::SIGN,blob,operation,[&](ErrorCode e,const auto&,uint64_t h) {
        ASSERT_EQ(e,ErrorCode::OK);handle=h;
    });
    ASSERT_NE(handle,0u);
    hidl_vec<uint8_t> message={1,2,3,4,5,6,7};
    hal->finish(handle,{},message,{},[&](ErrorCode e,const auto&,const auto& out) {
        ASSERT_EQ(e,ErrorCode::OK);signature=out;
    });
    ASSERT_EQ(signature.size(),256u);
    const uint8_t* ptr=pub.data();
    bssl::UniquePtr<EVP_PKEY> pk(d2i_PUBKEY(nullptr,&ptr,pub.size()));
    ASSERT_NE(pk,nullptr);
    bssl::UniquePtr<EVP_MD_CTX> digest(EVP_MD_CTX_new());
    ASSERT_EQ(EVP_DigestVerifyInit(digest.get(),nullptr,EVP_sha256(),nullptr,pk.get()),1);
    EXPECT_EQ(EVP_DigestVerify(digest.get(),signature.data(),signature.size(),message.data(),message.size()),1);
    EXPECT_EQ(hal->deleteKey(blob),ErrorCode::OK);
}
