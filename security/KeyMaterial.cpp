// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#include "KeyMaterial.h"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <cstdlib>
#include <memory>

namespace a3tee {
namespace {
template <typename T, void (*Free)(T*)> using Ptr = std::unique_ptr<T, decltype(Free)>;
int curve(int bits) {
    switch (bits) {
    case 224: return NID_secp224r1;
    case 256: return NID_X9_62_prime256v1;
    case 384: return NID_secp384r1;
    case 521: return NID_secp521r1;
    default: return NID_undef;
    }
}
bool appendBn(Message* message, size_t field, const BIGNUM* bn, size_t width = 0) {
    if (!bn || BN_is_negative(bn)) return false;
    size_t size = BN_num_bytes(bn);
    if (width && size > width) return false;
    if (!width) width = size;
    if (!width || width > 512) return false;
    std::vector<uint8_t> bytes(width);
    BN_bn2bin(bn, bytes.data()+width-size);
    bool ok = message->blob(field, bytes.data(), bytes.size());
    wipe(bytes.data(), bytes.size());
    return ok;
}
bool encodePublic(EVP_PKEY* key, keymaster_blob_t* out) {
    int size = i2d_PUBKEY(key, nullptr);
    if (size <= 0) return false;
    auto* bytes = static_cast<uint8_t*>(malloc(size));
    if (!bytes) return false;
    uint8_t* cursor = bytes;
    if (i2d_PUBKEY(key, &cursor) != size) { free(bytes); return false; }
    *out = {bytes, static_cast<size_t>(size)};
    return true;
}
}  // namespace
bool ecImport(const keymaster_blob_t& pkcs8, Message* internal) {
    if (!pkcs8.data || pkcs8.data_length > kSharedSize) return false;
    const uint8_t* cursor = pkcs8.data;
    Ptr<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free> info(
        d2i_PKCS8_PRIV_KEY_INFO(nullptr, &cursor, pkcs8.data_length), PKCS8_PRIV_KEY_INFO_free);
    if (!info || cursor != pkcs8.data+pkcs8.data_length) return false;
    Ptr<EVP_PKEY, EVP_PKEY_free> key(EVP_PKCS82PKEY(info.get()), EVP_PKEY_free);
    if (!key || EVP_PKEY_id(key.get()) != EVP_PKEY_EC) return false;
    Ptr<EC_KEY, EC_KEY_free> ec(EVP_PKEY_get1_EC_KEY(key.get()), EC_KEY_free);
    if (!ec || !EC_KEY_check_key(ec.get())) return false;
    const EC_GROUP* group = EC_KEY_get0_group(ec.get());
    int bits = EC_GROUP_get_degree(group);
    if (curve(bits) == NID_undef) return false;
    Ptr<BIGNUM, BN_free> x(BN_new(), BN_free), y(BN_new(), BN_free);
    if (!x || !y || !EC_POINT_get_affine_coordinates_GFp(group, EC_KEY_get0_public_key(ec.get()),
                                                       x.get(), y.get(), nullptr)) return false;
    // CSG3 EC import: bit count and three offset/length pairs (d, x, y).
    size_t width = (bits+7)/8;
    return internal->put(0, bits) && appendBn(internal, 4, EC_KEY_get0_private_key(ec.get()), width) &&
           appendBn(internal, 12, x.get(), width) && appendBn(internal, 20, y.get(), width);
}
bool publicKey(const Reply& response, keymaster_blob_t* der) {
    *der = {};
    if (!response.range(0, 20)) return false;
    size_t offset = response.word(12), size = response.word(16);
    if (offset < 20 || !response.range(offset, size)) return false;
    // Every nested offset is relative to this internal key representation.
    std::vector<uint8_t> bytes(response.at(offset), response.at(offset)+size);
    Reply internal(bytes);
    auto bnAt = [&](size_t field, size_t header) -> BIGNUM* {
        if (!internal.range(field, 8)) return nullptr;
        size_t pos = internal.word(field), n = internal.word(field+4);
        if (!n || n > 512 || pos < header || !internal.range(pos, n)) return nullptr;
        return BN_bin2bn(internal.at(pos), n, nullptr);
    };
    Ptr<EVP_PKEY, EVP_PKEY_free> key(EVP_PKEY_new(), EVP_PKEY_free);
    if (!key) return false;
    if (response.word(8) == KM_ALGORITHM_RSA) {
        if (size < 24) return false;
        Ptr<RSA, RSA_free> rsa(RSA_new(), RSA_free);
        Ptr<BIGNUM, BN_free> n(bnAt(0, 24), BN_free), e(bnAt(8, 24), BN_free);
        if (!rsa || !n || !e || !RSA_set0_key(rsa.get(), n.get(), e.get(), nullptr)) return false;
        n.release(); e.release();
        if (!EVP_PKEY_set1_RSA(key.get(), rsa.get())) return false;
    } else if (response.word(8) == KM_ALGORITHM_EC) {
        if (size < 28) return false;
        size_t width = internal.word(16);
        int bits = width == 66 ? 521 : width*8;
        int nid = curve(bits);
        if (nid == NID_undef || internal.word(24) != width) return false;
        Ptr<EC_KEY, EC_KEY_free> ec(EC_KEY_new_by_curve_name(nid), EC_KEY_free);
        Ptr<BIGNUM, BN_free> x(bnAt(12, 28), BN_free), y(bnAt(20, 28), BN_free);
        if (!ec || !x || !y || !EC_KEY_set_public_key_affine_coordinates(ec.get(), x.get(), y.get()) ||
            !EC_KEY_check_key(ec.get()) || !EVP_PKEY_set1_EC_KEY(key.get(), ec.get())) return false;
    } else return false;
    return encodePublic(key.get(), der);
}
bool signatureToWire(const keymaster_blob_t& der, Message* internal) {
    if (!der.data || !der.data_length || der.data_length > 144) return false;
    const uint8_t* cursor = der.data;
    Ptr<ECDSA_SIG, ECDSA_SIG_free> sig(d2i_ECDSA_SIG(nullptr, &cursor, der.data_length), ECDSA_SIG_free);
    if (!sig || cursor != der.data+der.data_length) return false;
    const BIGNUM *r, *s;
    ECDSA_SIG_get0(sig.get(), &r, &s);
    if (BN_is_zero(r) || BN_is_zero(s) || BN_num_bytes(r) > 66 || BN_num_bytes(s) > 66) return false;
    return appendBn(internal, 0, r) && appendBn(internal, 8, s);
}
bool signatureFromWire(const keymaster_blob_t& raw, keymaster_blob_t* der) {
    *der = {};
    if (!raw.data || !raw.data_length || raw.data_length%2 || raw.data_length > 132) return false;
    int width = raw.data_length/2;
    Ptr<ECDSA_SIG, ECDSA_SIG_free> sig(ECDSA_SIG_new(), ECDSA_SIG_free);
    Ptr<BIGNUM, BN_free> r(BN_bin2bn(raw.data, width, nullptr), BN_free);
    Ptr<BIGNUM, BN_free> s(BN_bin2bn(raw.data+width, width, nullptr), BN_free);
    if (!sig || !r || !s || BN_is_zero(r.get()) || BN_is_zero(s.get()) ||
        !ECDSA_SIG_set0(sig.get(), r.get(), s.get())) return false;
    r.release(); s.release();
    int n = i2d_ECDSA_SIG(sig.get(), nullptr);
    if (n <= 0) return false;
    auto* bytes = static_cast<uint8_t*>(malloc(n));
    if (!bytes) return false;
    uint8_t* cursor = bytes;
    if (i2d_ECDSA_SIG(sig.get(), &cursor) != n) { free(bytes); return false; }
    *der = {bytes, static_cast<size_t>(n)};
    return true;
}
}  // namespace a3tee
