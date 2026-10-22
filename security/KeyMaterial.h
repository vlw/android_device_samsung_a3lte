// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#pragma once
#include "Protocol.h"
namespace a3tee {
bool ecImport(const keymaster_blob_t& pkcs8, Message* internal);
bool publicKey(const Reply& response, keymaster_blob_t* der);
bool signatureToWire(const keymaster_blob_t& der, Message* internal);
bool signatureFromWire(const keymaster_blob_t& raw, keymaster_blob_t* der);
}
