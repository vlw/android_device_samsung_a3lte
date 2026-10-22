// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#pragma once
#include "Protocol.h"
#include <initializer_list>
#include <utility>

namespace a3tee {
using InitialReply = std::initializer_list<std::pair<size_t, uint32_t>>;
class Transport {
  public:
    virtual ~Transport() = default;
    virtual bool transact(const Message& request, std::vector<uint8_t>* response,
                          InitialReply initial = {}) = 0;
};
// Never falls back to software. Opening a TA can initialize secure storage, so
// call only from the explicitly selected experimental hardware services.
Transport* openQsee();
}  // namespace a3tee
