// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#pragma once
#include "Transport.h"
#include <hardware/keymaster1.h>
#include <memory>
namespace a3tee {
// Takes ownership of transport, returns a genuine KM1 HAL, not a software HAL
// relabelled as TEE. The caller must qualify/open the transport first.
keymaster1_device_t* makeKeymaster(const hw_module_t* module, std::unique_ptr<Transport> transport);
}
