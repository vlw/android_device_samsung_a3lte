// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#include "Protocol.h"
#include <cstdlib>
#include <cstring>
#include <limits>

namespace a3tee {
void wipe(void* data, size_t size) {
    auto* p = static_cast<volatile uint8_t*>(data);
    while (size--) *p++ = 0;
}
static uint32_t load(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
Message::Message(size_t header, uint32_t command) {
    if (header > kSharedSize) { valid_ = false; return; }
    data_.resize(header);
    if (header >= 4) put(0, command);
}
Message::~Message() { wipe(data_.data(), data_.size()); }
uint32_t Message::word(size_t offset) const {
    return offset <= data_.size() && data_.size() - offset >= 4 ? load(data_.data()+offset) : 0;
}
bool Message::put(size_t offset, uint32_t value) {
    if (!valid_ || offset > data_.size() || data_.size() - offset < 4) return valid_ = false;
    for (size_t i = 0; i < 4; ++i) data_[offset+i] = value >> (8*i);
    return true;
}
bool Message::put64(size_t offset, uint64_t value) {
    return put(offset, value) && put(offset+4, value >> 32);
}
bool Message::append(const void* data, size_t size, uint32_t* offset) {
    if (!valid_ || (size && !data) || size > kSharedSize - data_.size()) return valid_ = false;
    if (offset) *offset = data_.size();
    // Wipe the old allocation before a vector growth frees it: messages may
    // contain imported private keys, passwords or authentication tokens.
    if (data_.capacity() < data_.size() + size) {
        std::vector<uint8_t> replacement(data_.size()+size);
        if (!data_.empty()) memcpy(replacement.data(), data_.data(), data_.size());
        wipe(data_.data(), data_.size());
        data_.swap(replacement);
    } else {
        data_.resize(data_.size()+size);
    }
    if (size) memcpy(data_.data()+data_.size()-size, data, size);
    return true;
}
bool Message::blob(size_t field, const void* data, size_t size) {
    uint32_t offset = 0;
    return (!size || append(data, size, &offset)) && put(field, offset) && put(field+4, size);
}
bool Message::params(size_t field, const keymaster_key_param_set_t* params) {
    size_t count = params ? params->length : 0;
    if (count > kMaxParams || (count && !params->params)) return valid_ = false;
    if (!count) return put(field, 0) && put(field+4, 0);
    const size_t base = data_.size();
    std::vector<uint8_t> empty(count*kParamSize);
    if (!append(empty.data(), empty.size()) || !put(field, base) || !put(field+4, count)) return false;
    for (size_t i = 0; i < count; ++i) {
        const auto& p = params->params[i];
        const size_t offset = base + i*kParamSize;
        if (!put(offset, p.tag)) return false;
        switch (keymaster_tag_get_type(p.tag)) {
        case KM_ENUM: case KM_ENUM_REP: case KM_UINT: case KM_UINT_REP:
            if (!put(offset+4, p.integer)) return false;
            break;
        case KM_ULONG: case KM_ULONG_REP: case KM_DATE:
            if (!put64(offset+4, p.long_integer)) return false;
            break;
        case KM_BOOL:
            if (!put(offset+4, p.boolean ? 1 : 0)) return false;
            break;
        case KM_BYTES: case KM_BIGNUM: {
            uint32_t blobOffset;
            if (!append(p.blob.data, p.blob.data_length, &blobOffset) ||
                !put(offset+4, blobOffset-base) || !put(offset+8, p.blob.data_length)) return false;
            break;
        }
        default: return valid_ = false;
        }
    }
    return true;
}
bool Reply::range(size_t offset, size_t size) const {
    return offset <= data_.size() && size <= data_.size()-offset;
}
uint32_t Reply::word(size_t offset) const { return range(offset, 4) ? load(at(offset)) : 0; }
uint64_t Reply::wide(size_t offset) const { return uint64_t(word(offset)) | (uint64_t(word(offset+4))<<32); }
keymaster_error_t Reply::status() const {
    return range(0, 4) ? static_cast<keymaster_error_t>(word(0)) : KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
}
bool Reply::blob(size_t field, size_t minOffset, keymaster_blob_t* out) const {
    if (!out || !range(field, 8)) return false;
    *out = {};
    size_t offset = word(field), size = word(field+4);
    if (!size) return offset == 0 || range(offset, 0);
    if (offset < minOffset || !range(offset, size)) return false;
    auto* p = static_cast<uint8_t*>(malloc(size));
    if (!p) return false;
    memcpy(p, at(offset), size);
    *out = {p, size};
    return true;
}
void freeParams(keymaster_key_param_set_t* params) {
    if (!params) return;
    for (size_t i = 0; i < params->length; ++i) {
        auto& p = params->params[i];
        if (keymaster_tag_get_type(p.tag) == KM_BYTES || keymaster_tag_get_type(p.tag) == KM_BIGNUM) {
            wipe(const_cast<uint8_t*>(p.blob.data), p.blob.data_length);
            free(const_cast<uint8_t*>(p.blob.data));
        }
    }
    wipe(params->params, params->length*sizeof(*params->params));
    free(params->params);
    *params = {};
}
void freeCharacteristics(keymaster_key_characteristics_t* c) {
    if (c) { freeParams(&c->hw_enforced); freeParams(&c->sw_enforced); free(c); }
}
bool Reply::params(size_t offset, size_t count, size_t minOffset, keymaster_key_param_set_t* out) const {
    if (!out) return false;
    *out = {};
    if (count > kMaxParams || !range(offset, count*kParamSize) || (count && offset < minOffset)) return false;
    if (!count) return true;
    out->params = static_cast<keymaster_key_param_t*>(calloc(count, sizeof(*out->params)));
    if (!out->params) return false;
    out->length = count;
    for (size_t i = 0; i < count; ++i) {
        auto& p = out->params[i];
        size_t pos = offset + i*kParamSize;
        p.tag = static_cast<keymaster_tag_t>(word(pos));
        switch (keymaster_tag_get_type(p.tag)) {
        case KM_ENUM: case KM_ENUM_REP: case KM_UINT: case KM_UINT_REP:
            p.integer = word(pos+4); break;
        case KM_ULONG: case KM_ULONG_REP: case KM_DATE:
            p.long_integer = wide(pos+4); break;
        case KM_BOOL:
            if (word(pos+4) > 1) { freeParams(out); return false; }
            p.boolean = word(pos+4); break;
        case KM_BYTES: case KM_BIGNUM: {
            size_t relative = word(pos+4), size = word(pos+8);
            if (relative < count*kParamSize || relative > data_.size()-offset ||
                !range(offset+relative, size)) { freeParams(out); return false; }
            if (size) {
                auto* blob = static_cast<uint8_t*>(malloc(size));
                if (!blob) { freeParams(out); return false; }
                memcpy(blob, at(offset+relative), size);
                p.blob = {blob, size};
            }
            break;
        }
        default: freeParams(out); return false;
        }
    }
    return true;
}
}  // namespace a3tee
