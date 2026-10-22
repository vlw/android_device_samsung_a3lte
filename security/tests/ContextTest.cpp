// SPDX-License-Identifier: Apache-2.0
#include "Csg3Context.h"
#include "Keymaster.h"
#include <gtest/gtest.h>
#include <keymaster/key.h>
#include <keymaster/operation.h>
#include <openssl/evp.h>

namespace a3tee {
namespace {
using namespace keymaster;
struct RefuseHardware : Transport {
    size_t calls=0;
    bool transact(const Message&,std::vector<uint8_t>*,InitialReply) override {
        ++calls; return false;
    }
};
struct ContextTest : testing::Test {
    RefuseHardware* hardware;
    std::unique_ptr<Csg3Context> context;
    PureSoftKeymasterContext old{KmVersion::KEYMASTER_4_1};
    ContextTest() {
        auto t=std::make_unique<RefuseHardware>(); hardware=t.get();
        context=std::make_unique<Csg3Context>(makeKeymaster(nullptr,std::move(t)));
    }
    KeymasterKeyBlob legacy(bool auth=false) {
        AuthorizationSet p,hw,sw;
        p.push_back(TAG_ALGORITHM,KM_ALGORITHM_AES);p.push_back(TAG_KEY_SIZE,256);
        p.push_back(TAG_PURPOSE,KM_PURPOSE_ENCRYPT);p.push_back(TAG_BLOCK_MODE,KM_MODE_CTR);
        p.push_back(TAG_PADDING,KM_PAD_NONE);p.push_back(TAG_CALLER_NONCE);
        if(!auth) p.push_back(TAG_NO_AUTH_REQUIRED);
        else {p.push_back(TAG_USER_SECURE_ID,123456ULL);p.push_back(TAG_USER_AUTH_TYPE,HW_AUTH_PASSWORD);}
        uint8_t bytes[32]; memset(bytes,0x42,sizeof(bytes));
        KeymasterKeyBlob raw(bytes,sizeof(bytes)),result;
        EXPECT_EQ(old.CreateKeyBlob(p,KM_ORIGIN_IMPORTED,raw,&result,&hw,&sw),KM_ERROR_OK);
        return result;
    }
};
TEST_F(ContextTest, PreservesSoftwareAesOperationsWithoutCallingTee) {
    auto blob=legacy(); UniquePtr<Key> key;
    ASSERT_EQ(context->ParseKeyBlob(blob,{},&key),KM_ERROR_OK);
    ASSERT_NE(key,nullptr); EXPECT_TRUE(key->hw_enforced().empty());
    uint8_t nonce[16]{};AuthorizationSet params,outParams;
    params.push_back(TAG_BLOCK_MODE,KM_MODE_CTR);params.push_back(TAG_PADDING,KM_PAD_NONE);
    params.push_back(TAG_NONCE,nonce,sizeof(nonce));
    auto* factory=key->key_factory()->GetOperationFactory(KM_PURPOSE_ENCRYPT);
    ASSERT_NE(factory,nullptr);keymaster_error_t error=KM_ERROR_UNKNOWN_ERROR;
    auto op=factory->CreateOperation(std::move(*key),params,&error);
    ASSERT_EQ(error,KM_ERROR_OK);ASSERT_NE(op,nullptr);
    ASSERT_EQ(op->Begin(params,&outParams),KM_ERROR_OK);
    uint8_t message[31];memset(message,0x53,sizeof(message));Buffer input(message,sizeof(message)),signature,output;
    ASSERT_EQ(op->Finish({},input,signature,&outParams,&output),KM_ERROR_OK);
    ASSERT_EQ(output.available_read(),sizeof(message));
    uint8_t secret[32];memset(secret,0x42,sizeof(secret));
    uint8_t expected[64];int length=0,final=0;
    bssl::UniquePtr<EVP_CIPHER_CTX> cipher(EVP_CIPHER_CTX_new());
    ASSERT_EQ(EVP_EncryptInit_ex(cipher.get(),EVP_aes_256_ctr(),nullptr,secret,nonce),1);
    ASSERT_EQ(EVP_EncryptUpdate(cipher.get(),expected,&length,message,sizeof(message)),1);
    ASSERT_EQ(EVP_EncryptFinal_ex(cipher.get(),expected+length,&final),1);
    EXPECT_EQ(memcmp(output.peek_read(),expected,sizeof(message)),0);
    EXPECT_EQ(hardware->calls,0u);
}
TEST_F(ContextTest, RejectsTamperedSoftwareBlobWithoutSendingItToTee) {
    auto blob=legacy();blob.writable_data()[blob.key_material_size-1]^=1;UniquePtr<Key> key;
    EXPECT_EQ(context->ParseKeyBlob(blob,{},&key),KM_ERROR_INVALID_KEY_BLOB);
    EXPECT_EQ(hardware->calls,0u);
}
TEST_F(ContextTest, DoesNotBypassAuthenticationOnOldSoftwareKeys) {
    auto blob=legacy(true);UniquePtr<Key> key;
    EXPECT_EQ(context->ParseKeyBlob(blob,{},&key),KM_ERROR_KEY_USER_NOT_AUTHENTICATED);
    EXPECT_EQ(hardware->calls,0u);
}
} // namespace
} // namespace a3tee
