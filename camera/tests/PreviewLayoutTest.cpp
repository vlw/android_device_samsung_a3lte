/* SPDX-License-Identifier: Apache-2.0 */
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// CSG3 cam_frame_len_offset_t: plane count, eight 32-byte plane records,
// then total frame length. No sensor or camera device is opened by this test.
struct Plane { uint32_t length, offset, offsetX, offsetY, stride, scanline, width, height; };
struct Layout { uint32_t count; Plane planes[8]; uint32_t length; };
static_assert(sizeof(Layout) == 264, "CSG3 layout ABI");
int main(int argc, char** argv) {
    const bool baseline = argc == 2 && !strcmp(argv[1], "--baseline");
    const hw_module_t* module = nullptr;
    if (hw_get_module("gralloc", &module)) return 1;
    alloc_device_t* allocator = nullptr;
    if (gralloc_open(module, &allocator)) return 2;
    void* library = dlopen("libmmcamera_interface.so", RTLD_NOW);
    if (!library) { puts(dlerror()); return 3; }
    auto calculate = reinterpret_cast<int (*)(int, const int*, Layout*)>(
            dlsym(library, "mm_stream_calc_offset_preview"));
    if (!calculate) return 4;
    const int sizes[][2] = {{1920,1080},{1440,1080},{1280,720},{960,720},
        {960,540},{880,720},{800,480},{720,540},{720,480},{640,480},{528,432},
        {352,288},{320,240},{176,144},{1056,864},{720,720}};
    int failures = 0, mismatches = 0;
    for (const auto& size : sizes) {
        // cam_format_t NV21=2 / YV12=4; HAL format NV21=0x11 / YV12='YV12'.
        for (int format : {2, 4}) {
            Layout layout{};
            const int result = calculate(format, size, &layout);
            buffer_handle_t buffer = nullptr;
            int stride = 0;
            const int allocation = allocator->alloc(allocator, size[0], size[1],
                    format == 2 ? 0x11 : 0x32315659,
                    GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_CAMERA_WRITE,
                    &buffer, &stride);
            bool mismatch = layout.planes[0].stride != static_cast<uint32_t>(stride);
            uint32_t bytes = 0;
            for (uint32_t p = 0; p < layout.count && p < 8; ++p) bytes += layout.planes[p].length;
            bool valid = result == 0 && allocation == 0 && buffer &&
                    layout.count == (format == 2 ? 2u : 3u) &&
                    layout.planes[0].scanline == static_cast<uint32_t>(size[1]) &&
                    layout.planes[0].length == layout.planes[0].stride * size[1] &&
                    layout.length >= bytes;
            if (format == 2) valid = valid && layout.planes[1].stride == layout.planes[0].stride;
            printf("%s %dx%d HAL stride=%u gralloc=%d bytes=%u %s\n",
                    format == 2 ? "NV21" : "YV12", size[0], size[1],
                    layout.planes[0].stride, stride, layout.length,
                    !valid ? "INVALID" : mismatch ? "MISMATCH" : "OK");
            mismatches += mismatch;
            failures += !valid || (!baseline && mismatch);
            if (buffer) allocator->free(allocator, buffer);
        }
    }
    gralloc_close(allocator);
    dlclose(library);
    printf("%s 32 preview layouts; mismatches=%d; no sensors opened\n",
            failures ? "FAIL" : "PASS", mismatches);
    return failures ? 1 : 0;
}
