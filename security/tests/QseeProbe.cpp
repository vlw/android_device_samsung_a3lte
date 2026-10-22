// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
// Explicit diagnostic target. Never registered as a HAL or added to a product.
// No send_cmd symbol is resolved: even GET_VERSION can initialize TA RPMB state.
#include <dlfcn.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr char kDirectory[] = "/data/local/tmp/a3tee-csg3-probe";
constexpr char kControlDirectory[] = "/data/local/tmp/a3tee-bof7-probe";
constexpr char kApp[] = "a3kmprobe"; // Never use the pinned live keymaster name.
struct Handle { unsigned char* shared; };
struct Hash { const char* file; const char* hash; };
constexpr Hash kFirmware[] = {
#include "FirmwareHashes.inc"
};
constexpr Hash kControlFirmware[] = {
    {"keymaste.b00", "ced7e8b1946fa0e0cb89c2b4832993cad998298d2bc3e9e1d8628af41e386ed4"},
    {"keymaste.b01", "a030fd7fa8d2d05d7ce640b1a14fced05d18eee91012f06ff64de75699a1124c"},
    {"keymaste.b02", "db37715aa93e9cc5e82c70e990fbb3a22f2c96a6f9427a8e13f1a7d4ae4ff05d"},
    {"keymaste.b03", "145d24d815a0159d9b95b8d77b374678f4e99aa0311474d4075aa5728ad6090f"},
    {"keymaste.mdt", "658e2b5afe56e5a8b6953a2c70b21696a84ef28beb3b6d512068a62a04ea5f3d"},
};
bool checkFiles(bool control, bool candidate, bool vendor) {
    const Hash* begin = control ? kControlFirmware : kFirmware;
    size_t count = control ? sizeof(kControlFirmware)/sizeof(Hash) : sizeof(kFirmware)/sizeof(Hash);
    for (size_t index=0; index<count; ++index) {
        const auto& expected = begin[index];
        if (!vendor && strncmp(expected.file, "keymaste.", 9) &&
            strncmp(expected.file, "keymaster.", 10)) continue;
        const char* extension = strchr(expected.file, '.');
        std::string path = std::string(control ? kControlDirectory : kDirectory)+"/"+
                           (candidate ? "keymaster" : kApp)+extension;
        if (vendor) path = std::string("/vendor/firmware/")+expected.file;
        FILE* f = fopen(path.c_str(), "rbe");
        if (!f) { printf("Missing probe file: %s\n", path.c_str()); return false; }
        SHA256_CTX ctx; SHA256_Init(&ctx);
        std::array<unsigned char, 4096> block;
        size_t n;
        while ((n=fread(block.data(), 1, block.size(), f))) SHA256_Update(&ctx, block.data(), n);
        bool ok = !ferror(f); fclose(f);
        unsigned char digest[SHA256_DIGEST_LENGTH]; SHA256_Final(digest, &ctx);
        char hex[65];
        for (size_t i=0; i<32; ++i) snprintf(hex+2*i, 3, "%02x", digest[i]);
        if (!ok || strcmp(hex, expected.hash)) {
            printf("Probe firmware hash mismatch: %s\n", expected.file); return false;
        }
    }
    return true;
}
int versions() {
    int canonical = -1;
    int fd = open("/dev/qseecom", O_RDWR | O_CLOEXEC);
    if (fd<0) { printf("open qseecom errno=%d\n", errno); return -1; }
    uint32_t value = 0;
    int r = ioctl(fd, _IOWR(0x97, 10, uint32_t), &value);
    printf("QSEOS_VERSION rc=%d errno=%d value=0x%x\n", r, r<0?errno:0, value);
    value = 0;
    // Driver's read-only QSEECOM_IOCTL_GET_QSEE_VERSION_REQ, number 37.
    r = ioctl(fd, _IOWR(0x97, 37, uint32_t), &value);
    printf("QSEE_VERSION rc=%d errno=%d value=0x%x\n", r, r<0?errno:0, value);
    close(fd);
    // SCM lookup does not execute a TA. These two canonical names are pinned
    // by this kernel, so closing the query fd cannot unload the live Keymaster.
    struct Query { char name[32]; int id; };
    static_assert(sizeof(Query) == 36);
    for (const char* name : {"keymaster", "keymaste"}) {
        fd = open("/dev/qseecom", O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        Query query{}; snprintf(query.name, sizeof(query.name), "%s", name);
        errno = 0;
        r = ioctl(fd, _IOWR(0x97, 15, Query), &query);
        int saved = errno;
        printf("LOOKUP name=%s rc=%d errno=%d app_id=%d\n", name, r, r<0?saved:0, query.id);
        if (!strcmp(name, "keymaster")) {
            if (r == -1 && saved == EEXIST && query.id > 0) canonical = query.id;
            else if (!r) canonical = 0;
        }
        close(fd);
    }
    return canonical;
}
}
int main(int argc, char** argv) {
    setbuf(stdout, nullptr);
    if (argc != 2 || (strcmp(argv[1], "--inventory") && strcmp(argv[1], "--load-only") &&
                      strcmp(argv[1], "--load-installed-control") && strcmp(argv[1], "--attach-only") &&
                      strcmp(argv[1], "--load-candidate") && strcmp(argv[1], "--load-vendor"))) {
        puts("Usage: a3tee_qsee_probe --inventory | --load-only | --load-installed-control | --load-candidate | --load-vendor | --attach-only"); return 2;
    }
    int canonical = versions();
    if (!strcmp(argv[1], "--inventory")) return 0;
    bool control = !strcmp(argv[1], "--load-installed-control");
    bool attach = !strcmp(argv[1], "--attach-only");
    bool vendor = !strcmp(argv[1], "--load-vendor");
    bool candidate = vendor || !strcmp(argv[1], "--load-candidate");
    if (!attach && canonical != 0) {
        puts("Refusing fresh TA load: canonical keymaster exists or lookup failed.");
        puts("Signed TA has an internal name; a filename alias does not create isolation.");
        return 4;
    }
    if (attach && canonical <= 0) {
        puts("Refusing attach: canonical TA is not already loaded"); return 3;
    }
    if (!attach && !checkFiles(control, candidate, vendor)) return 2;
    void* library = dlopen("libQSEEComAPI.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { printf("dlopen QSEE: %s\n", dlerror()); return 3; }
    auto start = reinterpret_cast<int(*)(Handle**, const char*, const char*, uint32_t)>(
        dlsym(library, "QSEECom_start_app"));
    auto shutdown = reinterpret_cast<int(*)(Handle**)>(dlsym(library, "QSEECom_shutdown_app"));
    if (!start || !shutdown) { puts("Missing QSEE API entry points"); dlclose(library); return 3; }
    Handle* handle = nullptr;
    // Bound userspace execution. A kernel/secure-world fault cannot be made
    // recoverable by a userspace timeout; this is a qualification probe only.
    alarm(20);
    errno = 0;
    const char* directory = (attach || vendor) ? "/vendor/firmware" : control ? kControlDirectory : kDirectory;
    const char* app = (attach || candidate) ? "keymaster" : kApp;
    int r = start(&handle, directory, app, 0xa000);
    printf("start_app name=%s rc=%d errno=%d handle=%s shared=%s\n", app, r, r?errno:0,
           handle?"present":"absent", !r && handle && handle->shared?"present":"not inspected");
    int closeResult = 0;
    if (!r && handle) {
        errno = 0;
        closeResult = shutdown(&handle);
        printf("shutdown_app rc=%d errno=%d handle=%s\n", closeResult, closeResult?errno:0,
               handle?"present":"absent");
    }
    alarm(0);
    dlclose(library);
    puts("No TA commands, key operations or Gatekeeper operations sent.");
    return r || closeResult ? 1 : 0;
}
