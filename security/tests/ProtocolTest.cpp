// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#include "Gatekeeper.h"
#include "Keymaster.h"
#include "KeyMaterial.h"
#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <random>

namespace a3tee {
namespace {
void word(std::vector<uint8_t>& b, size_t pos, uint32_t value) {
    ASSERT_LE(pos+4, b.size());
    for (size_t i=0; i<4; ++i) b[pos+i] = value>>(8*i);
}
std::vector<uint8_t> reply(size_t size, int status = 0) {
    std::vector<uint8_t> b(size);
    word(b, 0, status);
    return b;
}
void release(keymaster_blob_t* b) { free(const_cast<uint8_t*>(b->data)); *b = {}; }
struct Fake : Transport {
    std::function<bool(const Message&, std::vector<uint8_t>*, InitialReply)> handler;
    size_t calls = 0;
    bool transact(const Message& req, std::vector<uint8_t>* res, InitialReply initial) override {
        ++calls;
        return handler ? handler(req, res, initial) : false;
    }
};
struct Device {
    Fake* transport;
    keymaster1_device_t* dev;
    Device() {
        auto fake = std::make_unique<Fake>(); transport = fake.get();
        dev = makeKeymaster(nullptr, std::move(fake));
    }
    ~Device() { dev->common.close(&dev->common); }
};
TEST(Wire, Packed32BitParametersAndRelativeBlobOffsets) {
    uint8_t secret[] = {0xde, 0xad, 0xbe, 0xef};
    keymaster_key_param_t p[3]{};
    p[0].tag = KM_TAG_ALGORITHM; p[0].enumerated = KM_ALGORITHM_AES;
    p[1].tag = KM_TAG_USER_SECURE_ID; p[1].long_integer = 0x1122334455667788ULL;
    p[2].tag = KM_TAG_AUTH_TOKEN; p[2].blob = {secret, sizeof(secret)};
    keymaster_key_param_set_t params{p, 3};
    Message request(12, 0x108);
    ASSERT_TRUE(request.params(4, &params));
    EXPECT_EQ(request.word(4), 12u); EXPECT_EQ(request.word(8), 3u);
    EXPECT_EQ(request.word(12), uint32_t(KM_TAG_ALGORITHM));
    EXPECT_EQ(request.word(16), 32u); // AES
    EXPECT_EQ(request.word(28), 0x55667788u);
    EXPECT_EQ(request.word(32), 0x11223344u);
    EXPECT_EQ(request.word(40), 36u); // relative to param array, not request
    EXPECT_EQ(request.word(44), 4u);
    auto bytes = request.bytes();
    keymaster_key_param_set_t decoded{};
    ASSERT_TRUE(Reply(bytes).params(12, 3, 12, &decoded));
    ASSERT_EQ(decoded.length, 3u);
    EXPECT_EQ(decoded.params[1].long_integer, p[1].long_integer);
    EXPECT_EQ(memcmp(decoded.params[2].blob.data, secret, 4), 0);
    freeParams(&decoded);
}
TEST(Wire, RejectsInvalidRangesOverflowAndUnknownTags) {
    auto bytes = reply(40);
    word(bytes, 8, KM_TAG_AUTH_TOKEN); word(bytes, 12, 0xffffffff); word(bytes, 16, 1);
    keymaster_key_param_set_t params{};
    EXPECT_FALSE(Reply(bytes).params(8, 1, 8, &params));
    EXPECT_EQ(params.params, nullptr);
    word(bytes, 12, 0); // cannot point back into the packed parameter table
    EXPECT_FALSE(Reply(bytes).params(8, 1, 8, &params));
    EXPECT_FALSE(Reply(bytes).params(8, SIZE_MAX, 8, &params));
    EXPECT_FALSE(Reply(bytes).range(SIZE_MAX, 1));
    Message req(8, 1);
    EXPECT_FALSE(req.append(nullptr, 1));
    Message huge(kSharedSize+1);
    EXPECT_FALSE(huge.valid());
}
TEST(Wire, MalformedReplyCorpus) {
    // Exercise the decoder with untrusted tags, offsets, lengths and integer
    // wraparound under ASan/UBSan. No firmware or TA is involved.
    std::mt19937 random(0xa300);
    for (int i=0; i<10000; ++i) {
        std::vector<uint8_t> bytes(random()%256);
        for (auto& b : bytes) b = random();
        Reply res(bytes);
        keymaster_key_param_set_t params{};
        if (res.params(random()%320, random()%140, 12, &params)) freeParams(&params);
        keymaster_blob_t b{};
        if (res.blob(random()%320, 16, &b)) release(&b);
    }
}
TEST(Keymaster, RejectsSoftwareLikeSuccessOnTransportFailure) {
    Device d;
    keymaster_key_param_set_t params{};
    keymaster_key_blob_t key{reinterpret_cast<const uint8_t*>(1), 9};
    keymaster_key_characteristics_t* chars = reinterpret_cast<keymaster_key_characteristics_t*>(1);
    EXPECT_EQ(d.dev->generate_key(d.dev, &params, &key, &chars), KM_ERROR_SECURE_HW_COMMUNICATION_FAILED);
    EXPECT_EQ(key.key_material, nullptr); EXPECT_EQ(chars, nullptr);
    EXPECT_EQ(d.dev->add_rng_entropy(d.dev, nullptr, 0), KM_ERROR_UNIMPLEMENTED);
    EXPECT_EQ(d.transport->calls, 1u); // no unsupported 0x107 command was sent
}
TEST(Keymaster, GeneratedBlobAndEnforcementLists) {
    Device d;
    d.transport->handler = [](const Message& req, auto* out, InitialReply) {
        EXPECT_EQ(req.word(0), 0x108u);
        *out = reply(49);
        word(*out, 8, 44); word(*out, 12, 5);
        word(*out, 16, 32); word(*out, 20, 12);
        word(*out, 32, KM_TAG_ALGORITHM); word(*out, 36, KM_ALGORITHM_AES);
        memcpy(out->data()+44, "BLOB!", 5);
        return true;
    };
    keymaster_key_param_set_t params{};
    keymaster_key_blob_t key{};
    keymaster_key_characteristics_t* chars = nullptr;
    ASSERT_EQ(d.dev->generate_key(d.dev, &params, &key, &chars), KM_ERROR_OK);
    ASSERT_NE(chars, nullptr);
    EXPECT_EQ(chars->hw_enforced.length, 1u);
    EXPECT_EQ(chars->sw_enforced.length, 0u);
    EXPECT_EQ(chars->hw_enforced.params[0].enumerated, KM_ALGORITHM_AES);
    EXPECT_EQ(key.key_material_size, 5u);
    free(const_cast<uint8_t*>(key.key_material)); freeCharacteristics(chars);
}
TEST(Keymaster, BeginUpdateFinishAndAbortLifetime) {
    Device d;
    int step = 0;
    d.transport->handler = [&](const Message& req, auto* out, InitialReply) {
        ++step;
        switch (req.word(0)) {
        case 0x10f:
            *out = reply(28); word(*out, 16, 42); word(*out, 24, KM_ALGORITHM_AES); return true;
        case 0x111:
            EXPECT_EQ(req.word(4), 42u);
            EXPECT_EQ(req.word(24), kChunkSize);
            *out = reply(20); word(*out, 16, kChunkSize); return true;
        case 0x112:
            EXPECT_EQ(req.word(12), 28u); // finish params must be placed here
            EXPECT_EQ(req.word(28), uint32_t(KM_TAG_AUTH_TOKEN));
            *out = reply(16); return true;
        default: ADD_FAILURE() << "unexpected command " << req.word(0); return false;
        }
    };
    uint8_t rawKey = 1;
    keymaster_key_blob_t key{&rawKey, 1};
    keymaster_operation_handle_t operation = 0;
    ASSERT_EQ(d.dev->begin(d.dev, KM_PURPOSE_ENCRYPT, &key, nullptr, nullptr, &operation), KM_ERROR_OK);
    ASSERT_EQ(operation, 42u);
    std::vector<uint8_t> input(kChunkSize+10);
    keymaster_blob_t in{input.data(), input.size()}, out{};
    size_t consumed = 0;
    ASSERT_EQ(d.dev->update(d.dev, operation, nullptr, &in, &consumed, nullptr, &out), KM_ERROR_OK);
    EXPECT_EQ(consumed, kChunkSize);
    uint8_t token[69]{};
    keymaster_key_param_t auth{}; auth.tag = KM_TAG_AUTH_TOKEN; auth.blob = {token, sizeof(token)};
    keymaster_key_param_set_t params{&auth, 1};
    EXPECT_EQ(d.dev->finish(d.dev, operation, &params, nullptr, nullptr, &out), KM_ERROR_OK);
    EXPECT_EQ(d.dev->abort(d.dev, operation), KM_ERROR_INVALID_OPERATION_HANDLE);
    EXPECT_EQ(step, 3);
}
TEST(Keymaster, MalformedOutputAbortsOperationWithoutLeakingData) {
    Device d;
    int aborted = 0;
    d.transport->handler = [&](const Message& req, auto* out, InitialReply) {
        if (req.word(0) == 0x10f) {
            *out = reply(28); word(*out, 16, 1); word(*out, 24, KM_ALGORITHM_AES);
        } else if (req.word(0) == 0x111) {
            *out = reply(20); word(*out, 8, 19); word(*out, 12, 0xfffffff0);
        } else if (req.word(0) == 0x113) { ++aborted; *out = reply(4); }
        else return false;
        return true;
    };
    uint8_t k = 1;
    keymaster_key_blob_t key{&k, 1}; keymaster_operation_handle_t op;
    ASSERT_EQ(d.dev->begin(d.dev, KM_PURPOSE_ENCRYPT, &key, nullptr, nullptr, &op), KM_ERROR_OK);
    size_t consumed; keymaster_blob_t out{};
    EXPECT_EQ(d.dev->update(d.dev, op, nullptr, nullptr, &consumed, nullptr, &out),
              KM_ERROR_SECURE_HW_COMMUNICATION_FAILED);
    EXPECT_EQ(out.data, nullptr); EXPECT_EQ(consumed, 0u); EXPECT_EQ(aborted, 1);
    EXPECT_EQ(d.dev->abort(d.dev, op), KM_ERROR_INVALID_OPERATION_HANDLE);
}
TEST(Ec, SignatureEncodingHandlesLeadingZeroBytes) {
    std::vector<uint8_t> raw(64);
    raw[0] = 0x80; raw[31] = 1; raw[32] = 0x90; raw[63] = 2;
    keymaster_blob_t der{};
    ASSERT_TRUE(signatureFromWire({raw.data(), raw.size()}, &der));
    EXPECT_EQ(der.data[0], 0x30u);
    Message internal(16);
    ASSERT_TRUE(signatureToWire(der, &internal));
    EXPECT_EQ(internal.word(0), 16u); EXPECT_EQ(internal.word(4), 32u);
    EXPECT_EQ(internal.word(8), 48u); EXPECT_EQ(internal.word(12), 32u);
    EXPECT_EQ(memcmp(internal.bytes().data()+16, raw.data(), raw.size()), 0);
    release(&der);
}
TEST(Ec, ImportPadsSmallPrivateScalarAndPublicExportRoundTrips) {
    // Fixed d=1 catches the stock client's BN_num_bytes(d)-based curve sizing
    // bug. P-521 requires 66-byte coordinates but a 521-bit curve identifier.
    for (auto [nid, bits] : {std::pair{NID_X9_62_prime256v1, 256}, std::pair{NID_secp521r1, 521}}) {
        EC_KEY* ec = EC_KEY_new_by_curve_name(nid);
        ASSERT_NE(ec, nullptr);
        BIGNUM* scalar = BN_new(); ASSERT_NE(scalar, nullptr); ASSERT_EQ(BN_one(scalar), 1);
        ASSERT_EQ(EC_KEY_set_private_key(ec, scalar), 1);
        const EC_GROUP* group = EC_KEY_get0_group(ec);
        EC_POINT* point = EC_POINT_new(group); ASSERT_NE(point, nullptr);
        ASSERT_EQ(EC_POINT_mul(group, point, scalar, nullptr, nullptr, nullptr), 1);
        ASSERT_EQ(EC_KEY_set_public_key(ec, point), 1);
        EVP_PKEY* key = EVP_PKEY_new(); ASSERT_NE(key, nullptr);
        ASSERT_EQ(EVP_PKEY_set1_EC_KEY(key, ec), 1);
        PKCS8_PRIV_KEY_INFO* pkcs8 = EVP_PKEY2PKCS8(key); ASSERT_NE(pkcs8, nullptr);
        int size = i2d_PKCS8_PRIV_KEY_INFO(pkcs8, nullptr); ASSERT_GT(size, 0);
        std::vector<uint8_t> der(size); uint8_t* cursor = der.data();
        ASSERT_EQ(i2d_PKCS8_PRIV_KEY_INFO(pkcs8, &cursor), size);
        Message internal(28);
        ASSERT_TRUE(ecImport({der.data(), der.size()}, &internal));
        EXPECT_EQ(internal.word(0), uint32_t(bits));
        EXPECT_EQ(internal.word(8), uint32_t((bits+7)/8));
        size_t d = internal.word(4), width = internal.word(8);
        EXPECT_EQ(internal.bytes()[d], 0u); EXPECT_EQ(internal.bytes()[d+width-1], 1u);
        // Build the TA public-export representation, with private material
        // removed, and compare its X.509 DER to OpenSSL's encoding.
        Message body(28); body.put(0, bits);
        body.blob(12, internal.bytes().data()+internal.word(12), internal.word(16));
        body.blob(20, internal.bytes().data()+internal.word(20), internal.word(24));
        Message wire(20); wire.put(8, KM_ALGORITHM_EC);
        wire.blob(12, body.bytes().data(), body.bytes().size());
        keymaster_blob_t exported{};
        ASSERT_TRUE(publicKey(Reply(wire.bytes()), &exported));
        size = i2d_PUBKEY(key, nullptr); ASSERT_GT(size, 0);
        der.resize(size); cursor = der.data(); ASSERT_EQ(i2d_PUBKEY(key, &cursor), size);
        ASSERT_EQ(exported.data_length, der.size());
        EXPECT_EQ(memcmp(exported.data, der.data(), der.size()), 0);
        release(&exported);
        PKCS8_PRIV_KEY_INFO_free(pkcs8); EVP_PKEY_free(key); EC_POINT_free(point); BN_free(scalar); EC_KEY_free(ec);
    }
}
TEST(Gatekeeper, RetryTimeoutAndTokenBytesRemainUntouched) {
    Fake transport;
    Gatekeeper backend(transport);
    uint8_t h = 1, p = 2;
    keymaster_blob_t handle{&h, 1}, pass{&p, 1}, out{};
    int phase = 0;
    transport.handler = [&](const Message& req, auto* res, InitialReply) {
        EXPECT_EQ(req.word(0), 0x1002u);
        EXPECT_EQ(req.bytes().size()%64, 0u);
        EXPECT_EQ(req.word(8), 0x55667788u); EXPECT_EQ(req.word(12), 0x11223344u);
        if (!phase++) *res = reply(12, 30000);
        else {
            *res = reply(81); word(*res, 4, 12); word(*res, 8, 69);
            for (size_t i=1; i<69; ++i) (*res)[12+i] = i;
        }
        return true;
    };
    EXPECT_EQ(backend.verify(10, 0x1122334455667788ULL, handle, pass, &out), 30000);
    EXPECT_EQ(out.data, nullptr);
    ASSERT_EQ(backend.verify(10, 0x1122334455667788ULL, handle, pass, &out), 0);
    ASSERT_EQ(out.data_length, 69u);
    for (size_t i=0; i<69; ++i) EXPECT_EQ(out.data[i], i);
    release(&out);
}
TEST(Gatekeeper, EnrollmentDoesNotDropAnExistingCredential) {
    Fake transport; Gatekeeper backend(transport);
    uint8_t h = 1, p = 2;
    keymaster_blob_t out{};
    EXPECT_EQ(backend.enroll(0, {&h, 1}, {}, {&p, 1}, &out), -EINVAL);
    EXPECT_EQ(transport.calls, 0u);
}
}  // namespace
}  // namespace a3tee
