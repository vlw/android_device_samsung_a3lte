#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Standalone ASan/UBSan check of the backend, with no QSEE transport linked.
set -euo pipefail
cd "$(dirname "$0")/../../../../.."
security_source=device/samsung/a3lte/security
security_out=$(realpath -m "${OUT_DIR:-out}")
security_test_out="$security_out/a3tee-tests"
mkdir -p "$security_test_out"
if [[ ! -f "$security_out/host/linux-x86/lib64/libcrypto-host.so" ]]; then
    echo 'Build a3tee_protocol_test first to provide the host BoringSSL library.' >&2
    exit 1
fi
"${A3TEE_HOST_CXX:-clang++}" -std=c++17 -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
    -I"$security_source" -Ihardware/libhardware/include \
    -Isystem/core/libcutils/include -Isystem/core/libsystem/include \
    -Iexternal/boringssl/src/include \
    -Iexternal/googletest/googletest/include -Iexternal/googletest/googletest \
    "$security_source/Protocol.cpp" "$security_source/KeyMaterial.cpp" \
    "$security_source/Keymaster.cpp" "$security_source/Gatekeeper.cpp" \
    "$security_source/tests/ProtocolTest.cpp" \
    external/googletest/googletest/src/gtest-all.cc \
    external/googletest/googletest/src/gtest_main.cc \
    -L"$security_out/host/linux-x86/lib64" -lcrypto-host -lpthread \
    -Wl,-rpath,"$security_out/host/linux-x86/lib64" \
    -o "$security_test_out/a3tee_protocol_test_asan"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$security_test_out/a3tee_protocol_test_asan"
