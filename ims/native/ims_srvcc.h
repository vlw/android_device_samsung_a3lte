// SPDX-License-Identifier: GPL-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>

bool initializeSamsungSrvcc(void* library, void** secRil,
    void (*notify)(int, const void*, size_t));
// Fixed, validated call-list protocol only; never an arbitrary IPC/OEM request.
bool submitSamsungSrvcc(const uint8_t* body, size_t length, unsigned count);
