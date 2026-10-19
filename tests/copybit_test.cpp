// SPDX-License-Identifier: Apache-2.0
// Exercises offscreen GPU buffers, without touching the display framebuffer.
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <copybit.h>
#include <cstdint>
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
struct Region {
    copybit_region_t region;
    mutable bool used = false;
    copybit_rect_t rect;
    static int next(const copybit_region_t* region, copybit_rect_t* rect) {
        const auto* self = reinterpret_cast<const Region*>(region);
        if (self->used) return 0;
        self->used = true;
        *rect = self->rect;
        return 1;
    }
};
struct Devices {
    alloc_device_t* alloc = nullptr;
    copybit_device_t* copy = nullptr;
    buffer_handle_t src = nullptr, dst = nullptr;
    ~Devices() {
        if (copy) copybit_close(copy);
        if (alloc) {
            if (src) alloc->free(alloc, src);
            if (dst) alloc->free(alloc, dst);
            gralloc_close(alloc);
        }
    }
};
int main() {
    const hw_module_t* raw = nullptr;
    CHECK(hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &raw) == 0);
    auto* gralloc = reinterpret_cast<const gralloc_module_t*>(raw);
    Devices dev;
    CHECK(gralloc_open(raw, &dev.alloc) == 0);
    CHECK(hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &raw) == 0);
    CHECK(copybit_open(raw, &dev.copy) == 0);
    constexpr int width = 64, height = 64;
    // HW_RENDER marks a GPU writer, so gralloc invalidates CPU caches on read.
    constexpr int usage = GRALLOC_USAGE_HW_2D | GRALLOC_USAGE_HW_RENDER |
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
    int srcStride = 0, dstStride = 0;
    CHECK(dev.alloc->alloc(dev.alloc, width, height, HAL_PIXEL_FORMAT_RGBA_8888, usage, &dev.src, &srcStride) == 0);
    CHECK(dev.alloc->alloc(dev.alloc, width, height, HAL_PIXEL_FORMAT_RGBA_8888, usage, &dev.dst, &dstStride) == 0);
    void* address = nullptr;
    CHECK(gralloc->lock(gralloc, dev.src, GRALLOC_USAGE_SW_WRITE_OFTEN, 0, 0, width, height, &address) == 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        static_cast<uint32_t*>(address)[y * srcStride + x] = x < width / 2 ? 0xff0000ff : 0xffff0000;
    CHECK(gralloc->unlock(gralloc, dev.src) == 0);
    copybit_image_t src{width, height, HAL_PIXEL_FORMAT_RGBA_8888, nullptr, const_cast<native_handle_t*>(dev.src), 0, 0};
    copybit_image_t dst{width, height, HAL_PIXEL_FORMAT_RGBA_8888, nullptr, const_cast<native_handle_t*>(dev.dst), 0, 0};
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_PLANE_ALPHA, 255) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_BLEND_MODE, COPYBIT_BLENDING_NONE) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_FRAMEBUFFER_WIDTH, width) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_FRAMEBUFFER_HEIGHT, height) == 0);
    Region region{{Region::next}, false, {0, 0, width, height}};
    CHECK(dev.copy->blit(dev.copy, &dst, &src, &region.region) == 0);
    CHECK(dev.copy->finish(dev.copy) == 0);
    CHECK(gralloc->lock(gralloc, dev.dst, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0, width, height, &address) == 0);
    unsigned mismatches = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        uint32_t expected = x < width / 2 ? 0xff0000ff : 0xffff0000;
        if (static_cast<uint32_t*>(address)[y * dstStride + x] != expected) ++mismatches;
    }
    CHECK(gralloc->unlock(gralloc, dev.dst) == 0);
    std::printf("C2D RGBA 64x64 offscreen copy: mismatches=%u\n", mismatches);
    CHECK(mismatches == 0);

    // Copybit clears per-draw framebuffer dimensions, as used by HWC.
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_FRAMEBUFFER_WIDTH, width) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_FRAMEBUFFER_HEIGHT, height) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_TRANSFORM, COPYBIT_TRANSFORM_ROT_90) == 0);
    region.used = false;
    CHECK(dev.copy->blit(dev.copy, &dst, &src, &region.region) == 0);
    CHECK(dev.copy->finish(dev.copy) == 0);
    CHECK(gralloc->lock(gralloc, dev.dst, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0, width, height, &address) == 0);
    mismatches = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        uint32_t expected = y < height / 2 ? 0xff0000ff : 0xffff0000;
        if (static_cast<uint32_t*>(address)[y * dstStride + x] != expected) ++mismatches;
    }
    CHECK(gralloc->unlock(gralloc, dev.dst) == 0);
    std::printf("C2D 90-degree rotation: mismatches=%u\n", mismatches);
    CHECK(mismatches == 0);

    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_FRAMEBUFFER_WIDTH, width) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_FRAMEBUFFER_HEIGHT, height) == 0);
    CHECK(dev.copy->set_parameter(dev.copy, COPYBIT_TRANSFORM, 0) == 0);
    copybit_rect_t full{0, 0, width, height}, half{0, 0, width / 2, height / 2};
    region.used = false;
    region.rect = half;
    CHECK(dev.copy->stretch(dev.copy, &dst, &src, &half, &full, &region.region) == 0);
    CHECK(dev.copy->finish(dev.copy) == 0);
    CHECK(gralloc->lock(gralloc, dev.dst, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0, width, height, &address) == 0);
    mismatches = 0;
    // Ignore the interpolated boundary and sample both interiors.
    for (int y = 2; y < height / 2 - 2; ++y) for (int x = 2; x < width / 2 - 2; ++x) {
        if (x >= width / 4 - 2 && x <= width / 4 + 2) continue;
        uint32_t expected = x < width / 4 ? 0xff0000ff : 0xffff0000;
        if (static_cast<uint32_t*>(address)[y * dstStride + x] != expected) ++mismatches;
    }
    CHECK(gralloc->unlock(gralloc, dev.dst) == 0);
    std::printf("C2D 64x64 -> 32x32 scaling: mismatches=%u\n", mismatches);
    CHECK(mismatches == 0);
    std::puts("PASS C2D HAL load, GPU allocation, copy, rotation, scaling and pixel verification");
}
