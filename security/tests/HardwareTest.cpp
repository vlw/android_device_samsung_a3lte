// SPDX-License-Identifier: Apache-2.0
// Explicit real-TEE tests. No deleteAllKeys/deleteAllUsers or real-user RPCs.
#include "Keymaster.h"
#include "Gatekeeper.h"
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>

namespace a3tee {
namespace {
constexpr uint32_t kTestUid = 999997;
using Bytes = std::vector<uint8_t>;
keymaster_key_param_t enumeration(keymaster_tag_t t, uint32_t v) {
    keymaster_key_param_t p{}; p.tag=t; p.enumerated=v; return p;
}
keymaster_key_param_t integer(keymaster_tag_t t, uint32_t v) {
    keymaster_key_param_t p{}; p.tag=t; p.integer=v; return p;
}
keymaster_key_param_t boolean(keymaster_tag_t t) {
    keymaster_key_param_t p{}; p.tag=t; p.boolean=true; return p;
}
keymaster_key_param_t wide(keymaster_tag_t t, uint64_t v) {
    keymaster_key_param_t p{}; p.tag=t; p.long_integer=v; return p;
}
keymaster_key_param_t blob(keymaster_tag_t t, const Bytes& v) {
    keymaster_key_param_t p{}; p.tag=t; p.blob={v.data(),v.size()}; return p;
}
void release(keymaster_blob_t& b) {
    wipe(const_cast<uint8_t*>(b.data),b.data_length);
    free(const_cast<uint8_t*>(b.data)); b={};
}
class Borrow final : public Transport {
  public:
    explicit Borrow(Transport& t):t_(t) {}
    bool transact(const Message& m, Bytes* b, InitialReply r) override {
        return t_.transact(m,b,r);
    }
  private: Transport& t_;
};
class Hardware : public testing::Test {
  protected:
    static std::unique_ptr<Transport> transport;
    static keymaster1_device_t* km;
    static void SetUpTestSuite() {
        transport.reset(openQsee());
        ASSERT_NE(transport,nullptr) << "TEE open/version/client initialization failed";
        km=makeKeymaster(nullptr,std::make_unique<Borrow>(*transport));
        ASSERT_NE(km,nullptr);
    }
    static void TearDownTestSuite() {
        if(km) km->common.close(&km->common);
        km=nullptr; transport.reset();
    }
    void SetUp() override { ASSERT_NE(km,nullptr); }
    void TearDown() override {
        for(auto& key:keys_) {
            // Only blobs generated/imported by this test instance.
            auto e=km->delete_key(km,&key);
            EXPECT_TRUE(e==KM_ERROR_OK || e==KM_ERROR_UNIMPLEMENTED) << e;
            wipe(const_cast<uint8_t*>(key.key_material),key.key_material_size);
            free(const_cast<uint8_t*>(key.key_material));
        }
    }
    keymaster_key_blob_t key(std::vector<keymaster_key_param_t> p, const Bytes* raw=nullptr) {
        keymaster_key_param_set_t params{p.data(),p.size()};
        keymaster_key_blob_t result{}; keymaster_key_characteristics_t* c=nullptr;
        keymaster_error_t e;
        if(raw) {
            keymaster_blob_t in{raw->data(),raw->size()};
            e=km->import_key(km,&params,KM_KEY_FORMAT_RAW,&in,&result,&c);
        } else e=km->generate_key(km,&params,&result,&c);
        EXPECT_EQ(e,KM_ERROR_OK);
        if(!e) {
            keys_.push_back(result);
            EXPECT_NE(c,nullptr);
            if(c) EXPECT_GT(c->hw_enforced.length,0u) << "No TEE-enforced authorizations";
        }
        freeCharacteristics(c); return result;
    }
    keymaster_error_t run(const keymaster_key_blob_t& key, keymaster_purpose_t purpose,
                         std::vector<keymaster_key_param_t> p, const Bytes& input,
                         Bytes* output, const Bytes* signature=nullptr) {
        output->clear();
        keymaster_key_param_set_t params{p.data(),p.size()}, outParams{};
        keymaster_operation_handle_t op=0;
        auto e=km->begin(km,purpose,&key,&params,&outParams,&op);
        freeParams(&outParams);
        if(e) return e;
        size_t pos=0;
        while(pos<input.size()) {
            size_t consumed=0; keymaster_blob_t out{}, in{input.data()+pos,input.size()-pos};
            e=km->update(km,op,nullptr,&in,&consumed,&outParams,&out);
            freeParams(&outParams);
            if(!e && out.data_length) output->insert(output->end(),out.data,out.data+out.data_length);
            release(out);
            if(e || !consumed) {
                km->abort(km,op);
                return e?e:KM_ERROR_UNKNOWN_ERROR;
            }
            pos+=consumed;
        }
        keymaster_blob_t out{}, sig{};
        if(signature) sig={signature->data(),signature->size()};
        e=km->finish(km,op,nullptr,signature?&sig:nullptr,&outParams,&out);
        freeParams(&outParams);
        if(!e && out.data_length) output->insert(output->end(),out.data,out.data+out.data_length);
        release(out); return e;
    }
    std::vector<keymaster_key_blob_t> keys_;
};
std::unique_ptr<Transport> Hardware::transport;
keymaster1_device_t* Hardware::km=nullptr;

TEST_F(Hardware, Capabilities) {
    keymaster_algorithm_t* values=nullptr; size_t count=0;
    ASSERT_EQ(km->get_supported_algorithms(km,&values,&count),KM_ERROR_OK);
    for(auto a:{KM_ALGORITHM_RSA,KM_ALGORITHM_EC,KM_ALGORITHM_AES,KM_ALGORITHM_HMAC})
        EXPECT_NE(std::find(values,values+count,a),values+count);
    free(values);
}
TEST_F(Hardware, AesGcmRoundTripAndBadTag) {
    auto k=key({enumeration(KM_TAG_ALGORITHM,KM_ALGORITHM_AES),integer(KM_TAG_KEY_SIZE,128),
        enumeration(KM_TAG_PURPOSE,KM_PURPOSE_ENCRYPT),enumeration(KM_TAG_PURPOSE,KM_PURPOSE_DECRYPT),
        enumeration(KM_TAG_BLOCK_MODE,KM_MODE_GCM),enumeration(KM_TAG_PADDING,KM_PAD_NONE),
        integer(KM_TAG_MIN_MAC_LENGTH,128),boolean(KM_TAG_CALLER_NONCE),boolean(KM_TAG_NO_AUTH_REQUIRED)});
    ASSERT_NE(k.key_material,nullptr);
    Bytes nonce(12,0x37), input(1997,0x53), encrypted,decrypted;
    auto p=std::vector<keymaster_key_param_t>{enumeration(KM_TAG_BLOCK_MODE,KM_MODE_GCM),
        enumeration(KM_TAG_PADDING,KM_PAD_NONE),integer(KM_TAG_MAC_LENGTH,128),blob(KM_TAG_NONCE,nonce)};
    ASSERT_EQ(run(k,KM_PURPOSE_ENCRYPT,p,input,&encrypted),KM_ERROR_OK);
    ASSERT_EQ(encrypted.size(),input.size()+16);
    ASSERT_EQ(run(k,KM_PURPOSE_DECRYPT,p,encrypted,&decrypted),KM_ERROR_OK);
    EXPECT_EQ(decrypted,input);
    encrypted.back()^=1;
    EXPECT_EQ(run(k,KM_PURPOSE_DECRYPT,p,encrypted,&decrypted),KM_ERROR_VERIFICATION_FAILED);
}
TEST_F(Hardware, HmacImportSignVerify) {
    Bytes secret(32,0x41), message(211,0x62), signature, empty;
    auto k=key({enumeration(KM_TAG_ALGORITHM,KM_ALGORITHM_HMAC),integer(KM_TAG_KEY_SIZE,256),
        enumeration(KM_TAG_PURPOSE,KM_PURPOSE_SIGN),enumeration(KM_TAG_PURPOSE,KM_PURPOSE_VERIFY),
        enumeration(KM_TAG_DIGEST,KM_DIGEST_SHA_2_256),integer(KM_TAG_MIN_MAC_LENGTH,128),
        boolean(KM_TAG_NO_AUTH_REQUIRED)},&secret);
    ASSERT_NE(k.key_material,nullptr);
    auto p=std::vector<keymaster_key_param_t>{enumeration(KM_TAG_DIGEST,KM_DIGEST_SHA_2_256),integer(KM_TAG_MAC_LENGTH,256)};
    ASSERT_EQ(run(k,KM_PURPOSE_SIGN,p,message,&signature),KM_ERROR_OK);
    uint8_t expected[EVP_MAX_MD_SIZE]; unsigned int length=0;
    ASSERT_NE(HMAC(EVP_sha256(),secret.data(),secret.size(),message.data(),message.size(),expected,&length),nullptr);
    EXPECT_EQ(signature,Bytes(expected,expected+length));
    EXPECT_EQ(run(k,KM_PURPOSE_VERIFY,p,message,&empty,&signature),KM_ERROR_OK);
    ASSERT_FALSE(signature.empty()); signature.back()^=1;
    EXPECT_EQ(run(k,KM_PURPOSE_VERIFY,p,message,&empty,&signature),KM_ERROR_VERIFICATION_FAILED);
}
TEST_F(Hardware, RsaSignExportVerify) {
    auto k=key({enumeration(KM_TAG_ALGORITHM,KM_ALGORITHM_RSA),integer(KM_TAG_KEY_SIZE,2048),
        wide(KM_TAG_RSA_PUBLIC_EXPONENT,65537),enumeration(KM_TAG_PURPOSE,KM_PURPOSE_SIGN),
        enumeration(KM_TAG_DIGEST,KM_DIGEST_SHA_2_256),enumeration(KM_TAG_PADDING,KM_PAD_RSA_PKCS1_1_5_SIGN),
        boolean(KM_TAG_NO_AUTH_REQUIRED)});
    ASSERT_NE(k.key_material,nullptr);
    Bytes message(137,0x33), signature;
    ASSERT_EQ(run(k,KM_PURPOSE_SIGN,{enumeration(KM_TAG_DIGEST,KM_DIGEST_SHA_2_256),
        enumeration(KM_TAG_PADDING,KM_PAD_RSA_PKCS1_1_5_SIGN)},message,&signature),KM_ERROR_OK);
    keymaster_blob_t pub{}; ASSERT_EQ(km->export_key(km,KM_KEY_FORMAT_X509,&k,nullptr,nullptr,&pub),KM_ERROR_OK);
    const uint8_t* ptr=pub.data; bssl::UniquePtr<EVP_PKEY> publicKey(d2i_PUBKEY(nullptr,&ptr,pub.data_length));
    ASSERT_NE(publicKey,nullptr); bssl::UniquePtr<EVP_MD_CTX> ctx(EVP_MD_CTX_new());
    ASSERT_EQ(EVP_DigestVerifyInit(ctx.get(),nullptr,EVP_sha256(),nullptr,publicKey.get()),1);
    EXPECT_EQ(EVP_DigestVerify(ctx.get(),signature.data(),signature.size(),message.data(),message.size()),1);
    release(pub);
}
TEST_F(Hardware, EcSignExportVerify) {
    auto k=key({enumeration(KM_TAG_ALGORITHM,KM_ALGORITHM_EC),integer(KM_TAG_KEY_SIZE,256),
        enumeration(KM_TAG_PURPOSE,KM_PURPOSE_SIGN),enumeration(KM_TAG_DIGEST,KM_DIGEST_NONE),
        boolean(KM_TAG_NO_AUTH_REQUIRED)});
    ASSERT_NE(k.key_material,nullptr);
    Bytes digest(32,0x63), signature;
    ASSERT_EQ(run(k,KM_PURPOSE_SIGN,{enumeration(KM_TAG_DIGEST,KM_DIGEST_NONE)},digest,&signature),KM_ERROR_OK);
    keymaster_blob_t pub{}; ASSERT_EQ(km->export_key(km,KM_KEY_FORMAT_X509,&k,nullptr,nullptr,&pub),KM_ERROR_OK);
    const uint8_t* ptr=pub.data; bssl::UniquePtr<EVP_PKEY> publicKey(d2i_PUBKEY(nullptr,&ptr,pub.data_length));
    ASSERT_NE(publicKey,nullptr); bssl::UniquePtr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(publicKey.get(),nullptr));
    ASSERT_EQ(EVP_PKEY_verify_init(ctx.get()),1);
    EXPECT_EQ(EVP_PKEY_verify(ctx.get(),signature.data(),signature.size(),digest.data(),digest.size()),1);
    release(pub);
}
TEST_F(Hardware, GatekeeperAndAuthBoundKey) {
    Gatekeeper gk(*transport);
    Bytes password(32), wrong(32,0xfe); ASSERT_EQ(RAND_bytes(password.data(),password.size()),1);
    keymaster_blob_t pass{password.data(),password.size()}, handle{}, token{};
    ASSERT_EQ(gk.enroll(kTestUid,{},{},pass,&handle),0);
    ASSERT_EQ(handle.data_length,58u);
    EXPECT_NE(handle.data[57],0) << "Handle is not hardware-backed";
    uint64_t sid=0; memcpy(&sid,handle.data+1,8); ASSERT_NE(sid,0u);
    ASSERT_EQ(gk.verify(kTestUid,0x314159,handle,pass,&token),0);
    Bytes hat(token.data,token.data+token.data_length); release(token);
    Bytes secret(32,0x52), message(57,0x67), signature;
    auto k=key({enumeration(KM_TAG_ALGORITHM,KM_ALGORITHM_HMAC),integer(KM_TAG_KEY_SIZE,256),
        enumeration(KM_TAG_PURPOSE,KM_PURPOSE_SIGN),enumeration(KM_TAG_DIGEST,KM_DIGEST_SHA_2_256),
        integer(KM_TAG_MIN_MAC_LENGTH,128),wide(KM_TAG_USER_SECURE_ID,sid),
        enumeration(KM_TAG_USER_AUTH_TYPE,1),integer(KM_TAG_AUTH_TIMEOUT,60)},&secret);
    ASSERT_NE(k.key_material,nullptr);
    auto p=std::vector<keymaster_key_param_t>{enumeration(KM_TAG_DIGEST,KM_DIGEST_SHA_2_256),integer(KM_TAG_MAC_LENGTH,256)};
    EXPECT_EQ(run(k,KM_PURPOSE_SIGN,p,message,&signature),KM_ERROR_KEY_USER_NOT_AUTHENTICATED);
    p.push_back(blob(KM_TAG_AUTH_TOKEN,hat));
    EXPECT_EQ(run(k,KM_PURPOSE_SIGN,p,message,&signature),KM_ERROR_OK);
    hat.back()^=1; p.back()=blob(KM_TAG_AUTH_TOKEN,hat);
    EXPECT_EQ(run(k,KM_PURPOSE_SIGN,p,message,&signature),KM_ERROR_KEY_USER_NOT_AUTHENTICATED);
    keymaster_blob_t bad{wrong.data(),wrong.size()};
    EXPECT_NE(gk.verify(kTestUid,0,handle,bad,&token),0);
    release(token); release(handle);
    EXPECT_EQ(gk.deleteUser(kTestUid),0);
    wipe(password.data(),password.size()); wipe(hat.data(),hat.size());
}
} // namespace
} // namespace a3tee
