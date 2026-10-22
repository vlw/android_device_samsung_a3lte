// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#pragma once

#include <hardware/keymaster_defs.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace a3tee {
constexpr size_t kSharedSize = 0xa000;
constexpr size_t kParamSize = 12;
constexpr size_t kMaxParams = 128;
constexpr size_t kChunkSize = 0x4000;
// QSEOS 0x14 requires both ION request and response on 64-byte boundaries.
constexpr size_t responseCapacity(size_t requestSize) {
    return requestSize <= kSharedSize ? kSharedSize-((requestSize+63)&~size_t(63)) : 0;
}
void wipe(void* data, size_t size);

// Wire offsets are relative to the containing request/response. Blob offsets in
// a parameter set are instead relative to the start of that parameter array.
class Message {
  public:
    explicit Message(size_t header = 0, uint32_t command = 0);
    ~Message();
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
    uint32_t word(size_t offset) const;
    bool put(size_t offset, uint32_t value);
    bool put64(size_t offset, uint64_t value);
    bool append(const void* data, size_t size, uint32_t* offset = nullptr);
    bool blob(size_t field, const void* data, size_t size);
    bool params(size_t field, const keymaster_key_param_set_t* params);
    const std::vector<uint8_t>& bytes() const { return data_; }
    bool valid() const { return valid_; }
  private:
    std::vector<uint8_t> data_;
    bool valid_ = true;
};

class Reply {
  public:
    explicit Reply(const std::vector<uint8_t>& data) : data_(data) {}
    bool range(size_t offset, size_t size) const;
    uint32_t word(size_t offset) const;
    uint64_t wide(size_t offset) const;
    keymaster_error_t status() const;
    bool blob(size_t field, size_t minOffset, keymaster_blob_t* out) const;
    bool params(size_t offset, size_t count, size_t minOffset,
                keymaster_key_param_set_t* out) const;
    const uint8_t* at(size_t offset) const { return data_.data() + offset; }
    size_t size() const { return data_.size(); }
  private:
    const std::vector<uint8_t>& data_;
};
void freeParams(keymaster_key_param_set_t* params);
void freeCharacteristics(keymaster_key_characteristics_t* characteristics);
}  // namespace a3tee
