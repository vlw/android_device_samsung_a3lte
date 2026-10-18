// Standalone ABI check for A300FU Marshmallow's Samsung SuperShot HDR engine.
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>
struct Image {
    int width, height, reserved, pitch, format;
    uint8_t* data;
};
struct Params {
    int height, width, flag, uiMode, type, scratch, useScratch;
};
static int progress(int, int, void*) { return 0; }
int main() {
    void* lib = dlopen("/vendor/lib/libsupershot.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    auto create = reinterpret_cast<int (*)(void**)>(dlsym(lib, "SS_Create"));
    auto init = reinterpret_cast<int (*)(void*, const Params*, const char*)>(dlsym(lib, "SS_Init"));
    auto run = reinterpret_cast<int (*)(void*, int, Image**, int, Image*, void*,
                                        decltype(&progress), void*)>(dlsym(lib, "SS_Run"));
    auto release = reinterpret_cast<int (*)(void*)>(dlsym(lib, "SS_Release"));
    if (!create || !init || !run || !release) return 2;
    constexpr int w = 640, h = 480;
    std::vector<uint8_t> frames[3], output(w * h * 2, 0);
    Image images[3], *inputs[3];
    for (int i = 0; i < 3; i++) {
        frames[i].resize(w * h * 2, 128);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int value = 8 + x * 220 / w + (y / 16 % 2) * 15;
                value = i == 0 ? value / 4 : i == 2 ? value * 4 : value;
                frames[i][y * w + x] = value > 255 ? 255 : value;
            }
        images[i] = {w, h, 0, w, 9, frames[i].data()};
        inputs[i] = &images[i];
    }
    Image out = {w, h, 0, w, 9, output.data()};
    Params params = {h, w, 1, 3, 4, 0, 0};
    void* handle = nullptr;
    int rc = create(&handle);
    printf("create=%d\n", rc);
    fflush(stdout);
    if (!rc) rc = init(handle, &params, nullptr);
    printf("init=%d\n", rc);
    fflush(stdout);
    uint8_t info[4096] = {};
    if (!rc) rc = run(handle, 0, inputs, 3, &out, info, progress, nullptr);
    printf("run=%d sample=%u,%u,%u\n", rc, output[0], output[w / 2], output[w - 1]);
    if (handle) release(handle);
    if (!rc && (output[w / 2] < 20 || output[w - 1] <= output[w / 2] ||
                !memcmp(output.data(), frames[1].data(), w * h * 3 / 2)))
        return 3;
    return rc;
}
