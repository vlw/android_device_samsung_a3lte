// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#define LOG_TAG "a3tee"
#include "Transport.h"
#include <android-base/properties.h>
#include <android-base/file.h>
#include <android-base/strings.h>
#include <dlfcn.h>
#include <log/log.h>
#include <openssl/sha.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

namespace a3tee {
namespace {
struct QseeHandle { uint8_t* shared; };
struct FirmwareHash { const char* file; const char* hash; };
constexpr FirmwareHash kFirmware[] = {
#include "FirmwareHashes.inc"
};
bool checkFirmware() {
    // These boot chains have passed CSG3 TA load qualification. This property
    // is a compatibility allowlist, not authentication of secure world.
    const auto bootloader = android::base::GetProperty("ro.bootloader", "");
    if (bootloader != "A300FUXXU1CSG3") {
        ALOGE("Unqualified A300FU boot chain");
        return false;
    }
    std::string firmwarePath;
    if (!android::base::ReadFileToString("/sys/module/firmware_class/parameters/path", &firmwarePath) ||
        (!android::base::Trim(firmwarePath).empty() &&
         android::base::Trim(firmwarePath) != "/firmware/image")) {
        ALOGE("Non-native global firmware override is active");
        return false;
    }
    for (const auto& expected : kFirmware) {
        // The common library and TA must both belong to the native partition.
        // Keymaster aliases only expand Samsung's FAT basename, not its content.
        const char* directory = strncmp(expected.file, "cmnlib.", 7) == 0
                                    ? "/firmware/image/" : "/system/etc/firmware/";
        std::string path = std::string(directory) + expected.file;
        std::unique_ptr<FILE, decltype(&fclose)> f(fopen(path.c_str(), "rbe"), fclose);
        if (!f) { ALOGE("Missing CSG3 firmware component %s", expected.file); return false; }
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        std::array<uint8_t, 4096> block;
        size_t n;
        while ((n = fread(block.data(), 1, block.size(), f.get())) != 0) SHA256_Update(&ctx, block.data(), n);
        if (ferror(f.get())) return false;
        uint8_t digest[SHA256_DIGEST_LENGTH];
        SHA256_Final(digest, &ctx);
        char hex[SHA256_DIGEST_LENGTH*2+1];
        for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hex+2*i, 3, "%02x", digest[i]);
        if (strcmp(hex, expected.hash)) { ALOGE("Unqualified firmware %s", expected.file); return false; }
    }
    return true;
}
class QseeTransport final : public Transport {
  public:
    ~QseeTransport() override {
        if (handle_) shutdown_(&handle_);
        if (library_) dlclose(library_);
    }
    bool open() {
        if (!checkFirmware()) return false;
        library_ = dlopen("libQSEEComAPI.so", RTLD_NOW | RTLD_LOCAL);
        if (!library_) { ALOGE("QSEE API unavailable: %s", dlerror()); return false; }
        start_ = reinterpret_cast<decltype(start_)>(dlsym(library_, "QSEECom_start_app"));
        shutdown_ = reinterpret_cast<decltype(shutdown_)>(dlsym(library_, "QSEECom_shutdown_app"));
        send_ = reinterpret_cast<decltype(send_)>(dlsym(library_, "QSEECom_send_cmd"));
        bandwidth_ = reinterpret_cast<decltype(bandwidth_)>(dlsym(library_, "QSEECom_set_bandwidth"));
        if (!start_ || !shutdown_ || !send_ || !bandwidth_) return false;
        if (start_(&handle_, "/system/etc/firmware", "keymaster", kSharedSize) || !handle_ || !handle_->shared) return false;
        Message version(4, 0x200);
        std::vector<uint8_t> response;
        if (!transact(version, &response)) return false;
        Reply reply(response);
        // CSG3 dispatcher at 0x44a6 returns API 1.0 / TA 1.9. A different
        // version needs its own protocol qualification, never blind dispatch.
        if (reply.status() != KM_ERROR_OK || !reply.range(0, 20) || reply.word(4) != 1 ||
            reply.word(8) != 0 || reply.word(12) != 1 || reply.word(16) != 9) {
            ALOGE("Unqualified Keymaster TA version"); return false;
        }
        Message client(24, 0x207);
        client.put(4, 0x100); // HAL 1.0
        client.put(8, 1); client.put(12, 1); client.put(16, 1); client.put(20, 1);
        if (!transact(client, &response) || Reply(response).status() != KM_ERROR_OK) return false;
        return true;
    }
    bool transact(const Message& request, std::vector<uint8_t>* response,
                  InitialReply initial = {}) override {
        if (!response) return false;
        if (!response->empty()) wipe(response->data(), response->size());
        response->clear();
        size_t wireSize = request.bytes().size();
        if (!request.valid() || wireSize < 4 || wireSize > kSharedSize-256 || !handle_) return false;
        size_t capacity = responseCapacity(wireSize);
        size_t size = kSharedSize-capacity;
        // The fixed control replies are 20/4 bytes, padded for QSEOS 0x14.
        if (request.word(0) == 0x200 || request.word(0) == 0x207) capacity = 64;
        std::lock_guard<std::mutex> lock(mutex_);
        uint8_t* shared = handle_->shared;
        wipe(shared, kSharedSize);
        memcpy(shared, request.bytes().data(), wireSize);
        // Detect a TA command that returns without filling in its response.
        const uint32_t sentinel = uint32_t(KM_ERROR_SECURE_HW_COMMUNICATION_FAILED);
        for (size_t i = 0; i < 4; ++i) shared[size+i] = sentinel >> (8*i);
        for (const auto& [offset, value] : initial) {
            if (offset > capacity || capacity-offset < 4) { wipe(shared, kSharedSize); return false; }
            for (size_t i = 0; i < 4; ++i) shared[size+offset+i] = value >> (8*i);
        }
        int boosted = bandwidth_(handle_, true);
        int result = boosted < 0 ? boosted : send_(handle_, shared, size, shared+size, capacity);
        if (boosted >= 0) bandwidth_(handle_, false);
        if (!result) response->assign(shared+size, shared+size+capacity);
        wipe(shared, kSharedSize);
        if (result) ALOGE("QSEE command 0x%x failed: %d", request.word(0), result);
        return result == 0;
    }
  private:
    void* library_ = nullptr;
    QseeHandle* handle_ = nullptr;
    int (*start_)(QseeHandle**, const char*, const char*, uint32_t) = nullptr;
    int (*shutdown_)(QseeHandle**) = nullptr;
    int (*send_)(QseeHandle*, void*, uint32_t, void*, uint32_t) = nullptr;
    int (*bandwidth_)(QseeHandle*, bool) = nullptr;
    std::mutex mutex_;
};
}  // namespace
Transport* openQsee() {
    auto t = std::make_unique<QseeTransport>();
    if (!t->open()) return nullptr;
    return t.release();
}
}  // namespace a3tee
