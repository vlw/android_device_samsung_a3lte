// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#include "Gatekeeper.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
namespace a3tee {
namespace {
struct Response : std::vector<uint8_t> { ~Response() { wipe(data(), size()); } };
bool valid(const keymaster_blob_t& b) {
    return b.data && b.data_length && b.data_length < kSharedSize;
}
}
int Gatekeeper::send(Message& request, std::vector<uint8_t>* response) {
    // Samsung's Gatekeeper aligns to the NEXT 64-byte boundary, including
    // when the unpadded request already ends on a boundary.
    size_t padding = 64-(request.bytes().size()%64);
    uint8_t zero[64]{};
    if (!request.append(zero, padding) || request.bytes().size() > kSharedSize-256) return -EINVAL;
    if (!transport_.transact(request, response) || response->size() < 12) return -EIO;
    // Positive status is the TEE-enforced retry timeout, in milliseconds.
    return static_cast<int32_t>(Reply(*response).word(0));
}
int Gatekeeper::enroll(uint32_t uid, const keymaster_blob_t& handle,
                       const keymaster_blob_t& current, const keymaster_blob_t& desired,
                       keymaster_blob_t* out) {
    if (!out) return -EINVAL;
    *out = {};
    if (!valid(desired) || (handle.data_length && (!valid(handle) || !valid(current))) ||
        (!handle.data_length && current.data_length)) return -EINVAL;
    std::lock_guard<std::mutex> lock(mutex_);
    Message req(32, 0x1001);
    req.put(4, uid);
    if (handle.data_length) {
        req.blob(8, handle.data, handle.data_length);
        req.blob(16, current.data, current.data_length);
    }
    req.blob(24, desired.data, desired.data_length);
    Response bytes;
    int status = send(req, &bytes);
    if (status) return status;
    Reply res(bytes);
    if (!res.blob(4, 12, out) || !out->data_length) return -EIO;
    return 0;
}
int Gatekeeper::verify(uint32_t uid, uint64_t challenge, const keymaster_blob_t& handle,
                       const keymaster_blob_t& password, keymaster_blob_t* token) {
    if (!token) return -EINVAL;
    *token = {};
    if (!valid(handle) || !valid(password)) return -EINVAL;
    std::lock_guard<std::mutex> lock(mutex_);
    Message req(32, 0x1002);
    req.put(4, uid); req.put64(8, challenge);
    req.blob(16, handle.data, handle.data_length);
    req.blob(24, password.data, password.data_length);
    Response bytes;
    int status = send(req, &bytes);
    if (status) return status;
    Reply res(bytes);
    if (!res.blob(4, 12, token)) return -EIO;
    // Android hw_auth_token_t is packed, 69 bytes, version 0. All token bytes
    // (including mixed-endian fields and HMAC) must pass through unchanged.
    if (token->data_length != 69 || token->data[0] != 0) {
        wipe(const_cast<uint8_t*>(token->data), token->data_length);
        free(const_cast<uint8_t*>(token->data)); *token = {};
        return -EIO;
    }
    return 0;
}
int Gatekeeper::deleteUser(uint32_t uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    // send adds alignment. Delete requests in stock occupy exactly 64 bytes.
    Message req(8, 0x1003); req.put(4, uid); Response bytes;
    return send(req, &bytes);
}
int Gatekeeper::deleteAllUsers() {
    std::lock_guard<std::mutex> lock(mutex_);
    Message req(4, 0x1004); Response bytes;
    return send(req, &bytes);
}
}  // namespace a3tee
