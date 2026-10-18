/* SPDX-License-Identifier: Apache-2.0 */
// Diagnostic only: run ExifManager/MakerNote from the owner's firmware without
// starting the obsolete camera framework or invoking any camera device.
#include <camera/CameraParameters.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <utils/String8.h>

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
static time_t fixedTime(time_t *out) {
    if (out) *out = 1700000000;
    return 1700000000;
}
static int64_t getInt64(const android::CameraParameters *parameters, const char *key) {
    const char *value = parameters->get(key);
    return value ? strtoll(value, nullptr, 0) : -1;
}
int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) return 1;
    std::string prefix = argv[1];
    void *exif = dlopen("/data/local/tmp/libexifa.so", RTLD_NOW | RTLD_LOCAL);
    void *jpeg = dlopen("/data/local/tmp/libjpega.so", RTLD_NOW | RTLD_LOCAL);
    if (!exif || !jpeg) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    std::ifstream file(prefix + ".image", std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    auto *image =
        static_cast<unsigned char *>(mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE | PROT_EXEC,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (bytes.empty() || image == MAP_FAILED) return 3;
    memcpy(image, bytes.data(), bytes.size());
    std::map<std::string, std::string> constants;
    std::ifstream c(prefix + ".constants");
    std::string name, hex;
    while (c >> name >> hex) {
        std::string s;
        for (size_t i = 0; i < hex.size(); i += 2)
            s += char(strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
        constants[name] = s;
    }
    std::ifstream rel(prefix + ".relocs");
    unsigned offset, type, value;
    while (rel >> offset >> type >> value >> name) {
        auto *place = reinterpret_cast<uint32_t *>(image + offset);
        uintptr_t symbol = 0;
        if (value)
            symbol = reinterpret_cast<uintptr_t>(image + value);
        else if (constants.count(name))
            symbol = reinterpret_cast<uintptr_t>(constants[name].c_str());
        else if (name == "_ZNK7android16CameraParameters8getInt64EPKc")
            symbol = reinterpret_cast<uintptr_t>(getInt64);
        else if (name == "time")
            symbol = reinterpret_cast<uintptr_t>(fixedTime);
        else {
            void *p = dlsym(exif, name.c_str());
            if (!p) p = dlsym(jpeg, name.c_str());
            if (!p) p = dlsym(RTLD_DEFAULT, name.c_str());
            symbol = reinterpret_cast<uintptr_t>(p);
        }
        if (type == 23)
            *place += reinterpret_cast<uintptr_t>(image);
        else if (type == 2)
            *place += symbol;
        else if (type == 21 || type == 22)
            *place = symbol;
        else if (type != 0) {
            fprintf(stderr, "Unsupported relocation %u\n", type);
            return 4;
        }
    }
    __builtin___clear_cache(reinterpret_cast<char *>(image),
                            reinterpret_cast<char *>(image + bytes.size()));
    std::map<std::string, unsigned> symbols;
    std::ifstream sym(prefix + ".symbols");
    while (sym >> value >> name) symbols[name] = value;
    auto lookup = [&](const char *n) {
        if (!symbols.count(n)) {
            fprintf(stderr, "Missing %s\n", n);
            exit(5);
        }
        return image + symbols[n];
    };
    alignas(8) unsigned char note[256] = {}, manager[36] = {};
    reinterpret_cast<void (*)(void *)>(lookup("_ZN7android9MakerNoteC1Ev"))(note);
    reinterpret_cast<void (*)(void *)>(lookup("_ZN7android9MakerNote5resetEv"))(note);
    reinterpret_cast<void (*)(void *, unsigned)>(
        lookup("_ZN7android9MakerNote18setSamsungDeviceIDEj"))(note, 0x12000);
    reinterpret_cast<void (*)(void *, bool)>(lookup("_ZN7android9MakerNote18setFavoriteTaggingEb"))(
        note, false);
    reinterpret_cast<void (*)(void *, unsigned)>(lookup("_ZN7android9MakerNote13setColorSpaceEj"))(
        note, 1);
    if (argc == 6) {
        std::ifstream ae(argv[5], std::ios::binary);
        std::vector<unsigned char> debug((std::istreambuf_iterator<char>(ae)), {});
        if (debug.empty() || debug.size() > 50000) return 8;
        reinterpret_cast<void (*)(void *, unsigned char *, unsigned)>(
            lookup("_ZN7android9MakerNote10setAEDebugEPhj"))(note, debug.data(), debug.size());
    }
    reinterpret_cast<void (*)(void *)>(lookup("_ZN7android11ExifManagerC1Ev"))(manager);
    std::ifstream in(argv[2], std::ios::binary);
    std::vector<unsigned char> input((std::istreambuf_iterator<char>(in)), {});
    reinterpret_cast<void (*)(void *, void *, unsigned char *, unsigned)>(
        lookup("_ZN7android11ExifManager8SetInputEPNS_9MakerNoteEPhj"))(manager, note, input.data(),
                                                                        input.size());
    android::CameraParameters params{android::String8(argv[3])};
    params.set("scene-mode", "other");
    std::vector<unsigned char> output(input.size() + 102400);
    unsigned size = 0;
    reinterpret_cast<void (*)(void *, unsigned char *, unsigned *,
                              const android::CameraParameters &, bool)>(
        lookup("_ZN7android11ExifManager13GetResultJpegEPhPjRKNS_"
               "16CameraParametersEb"))(manager, output.data(), &size, params, false);
    if (size > output.size() || size < 4) return 6;
    FILE *out = fopen(argv[4], "wb");
    if (!out) return 7;
    fwrite(output.data(), 1, size, out);
    fclose(out);
    reinterpret_cast<void (*)(void *)>(lookup("_ZN7android11ExifManagerD1Ev"))(manager);
    reinterpret_cast<void (*)(void *)>(lookup("_ZN7android9MakerNoteD1Ev"))(note);
    printf("Original Samsung HDR ExifManager: %u bytes\n", size);
    return 0;
}
