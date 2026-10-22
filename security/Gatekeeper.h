// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#pragma once
#include "Transport.h"
#include <mutex>
namespace a3tee {
class Gatekeeper {
  public:
    explicit Gatekeeper(Transport& transport) : transport_(transport) {}
    int enroll(uint32_t uid, const keymaster_blob_t& handle, const keymaster_blob_t& current,
               const keymaster_blob_t& desired, keymaster_blob_t* out);
    int verify(uint32_t uid, uint64_t challenge, const keymaster_blob_t& handle,
               const keymaster_blob_t& password, keymaster_blob_t* token);
    int deleteUser(uint32_t uid);
    int deleteAllUsers();
  private:
    int send(Message& request, std::vector<uint8_t>* response);
    Transport& transport_;
    std::mutex mutex_;
};
}
