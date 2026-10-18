/* SPDX-License-Identifier: Apache-2.0 */
// Native Samsung HDR protocol test with synthetic NV21; never opens a sensor.
#include <camera/CameraParameters.h>
#include <jpeglib.h>
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils/String8.h>

#include <chrono>

#include "../StockHdr.h"
using android::CameraParameters;
using android::String8;
using namespace std::chrono_literals;
#define CHECK(test)                                                 \
    do {                                                            \
        if (!(test)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); \
            exit(1);                                                \
        }                                                           \
    } while (0)
struct Fake {
    camera_device_t device = {};
    camera_device_ops_t ops = {};
    StockHdr* hdr = nullptr;
    CameraParameters params;
    std::string saved;
    std::vector<uint8_t> images[3], result;
    int takes = 0;
    uint32_t messages = 0;
    bool reenter = false, blockCallback = false, releaseCallback = false;
    std::atomic<bool> callbackEntered{false};
    bool preview = true, invalid = false, shortFrame = false, drop = false;
    std::atomic<bool> captureEntered{false};
    std::mutex mutex;
    std::condition_variable condition;
    int completed = 0, errors = 0;
    Fake(int w, int h) {
        device.ops = &ops;
        device.priv = this;
        params.unflatten(
            String8("scene-mode=auto;exposure-compensation=7;auto-exposure-lock=true;auto-"
                    "whitebalance-lock=false;flash-mode=off;jpeg-quality=96;picture-format=nv21;"
                    "hdr-mode=1;capture-burst-exposures=-2,0,2;zsl=off;rotation=90"));
        params.setPictureSize(w, h);
        saved = params.flatten().string();
        ops.get_parameters = [](camera_device_t* d) {
            return strdup(self(d)->params.flatten().string());
        };
        ops.put_parameters = [](camera_device_t*, char* p) { free(p); };
        ops.set_parameters = [](camera_device_t* d, const char* p) {
            self(d)->params.unflatten(String8(p));
            return 0;
        };
        ops.preview_enabled = [](camera_device_t* d) { return static_cast<int>(self(d)->preview); };
        ops.start_preview = [](camera_device_t* d) {
            self(d)->preview = true;
            return 0;
        };
        ops.stop_preview = [](camera_device_t* d) { self(d)->preview = false; };
        ops.enable_msg_type = [](camera_device_t* d, int32_t bits) { self(d)->messages |= bits; };
        ops.disable_msg_type = [](camera_device_t* d, int32_t bits) { self(d)->messages &= ~bits; };
        ops.msg_type_enabled = [](camera_device_t* d, int32_t bits) -> int { return (self(d)->messages & bits) != 0; };
        ops.cancel_picture = [](camera_device_t*) { return 0; };
        ops.take_picture = [](camera_device_t* d) {
            Fake* f = self(d);
            ++f->takes;
            CHECK(f->params.flatten().string() == f->saved);
            f->preview = false;
            f->captureEntered = true;
            if (f->drop) return 0;
            for (int i = 0; i < 3; ++i) {
                camera_memory_t frame = {};
                frame.data = f->invalid ? nullptr : f->images[i].data();
                frame.size = f->shortFrame ? 4 : f->images[i].size();
                CHECK(f->hdr->onData(CAMERA_MSG_COMPRESSED_IMAGE, &frame, 0));
            }
            return 0;
        };
        for (int i = 0; i < 3; ++i) {
            images[i].resize(w * h * 3 / 2, 128);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    int value = 8 + x * 220 / w + (y / 16 % 2) * 15;
                    value = i == 0 ? value / 4 : i == 2 ? value * 4 : value;
                    images[i][y * w + x] = value > 255 ? 255 : value;
                }
        }
    }
    static Fake* self(camera_device_t* d) { return static_cast<Fake*>(d->priv); }
    static void complete(void* user, const std::vector<uint8_t>& jpeg) {
        auto* f = static_cast<Fake*>(user);
        if (f->reenter) {
            f->hdr->cancel();
            CHECK(f->hdr->takePicture() == -EBUSY);
        }
        std::unique_lock<std::mutex> lock(f->mutex);
        f->callbackEntered = true;
        if (f->blockCallback)
            CHECK(f->condition.wait_for(lock, 5s, [&] { return f->releaseCallback; }));
        f->result = jpeg;
        ++f->completed;
        f->condition.notify_all();
    }
    static void error(void* user) {
        auto* f = static_cast<Fake*>(user);
        std::lock_guard<std::mutex> lock(f->mutex);
        ++f->errors;
        f->condition.notify_all();
    }
    void wait(int success, int failure) {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(condition.wait_for(lock, 20s,
                                 [&] { return completed == success && errors == failure; }));
        lock.unlock();
        hdr->cancel();
        for (int i = 0; i < 1000 && hdr->busy(); ++i) std::this_thread::sleep_for(1ms);
        CHECK(!hdr->busy());
        CHECK(params.flatten().string() == saved);
        CHECK(!preview && !hdr->active());
    }
};
int main(int argc, char** argv) {
    int w = argc == 3 ? atoi(argv[1]) : 640, h = argc == 3 ? atoi(argv[2]) : 480;
    CHECK(w >= 64 && h >= 64 && w <= 3264 && h <= 3264 && !(w & 1) && !(h & 1));
    Fake fake(w, h);
    StockHdr hdr(&fake.device, Fake::complete, Fake::error, &fake);
    fake.hdr = &hdr;
    CHECK(hdr.available());
    hdr.enableMessages(CAMERA_MSG_COMPRESSED_IMAGE | CAMERA_MSG_ERROR | CAMERA_MSG_SHUTTER);
    int repeats = argc == 3 ? 1 : 2;
    for (int i = 1; i <= repeats; ++i) {
        CHECK(hdr.takePicture() == 0);
        CHECK(hdr.takePicture() == -EBUSY);
        std::string parameters;
        CHECK(hdr.parameters(&parameters) && parameters == fake.saved);
        CHECK(!hdr.onNotify(CAMERA_MSG_SHUTTER));
        CHECK(hdr.onNotify(CAMERA_MSG_SHUTTER));
        fake.wait(i, 0);
        CHECK(fake.takes == i);
        CHECK(fake.result.size() > 1000 && fake.result[0] == 0xff && fake.result[1] == 0xd8);
        jpeg_decompress_struct jpeg = {};
        jpeg_error_mgr error;
        jpeg.err = jpeg_std_error(&error);
        jpeg_create_decompress(&jpeg);
        jpeg_mem_src(&jpeg, fake.result.data(), fake.result.size());
        CHECK(jpeg_read_header(&jpeg, TRUE) == JPEG_HEADER_OK);
        CHECK(jpeg.image_width == static_cast<unsigned>(w) &&
              jpeg.image_height == static_cast<unsigned>(h));
        jpeg_destroy_decompress(&jpeg);
        ExifData* exif = exif_data_new_from_data(fake.result.data(), fake.result.size());
        CHECK(exif);
        auto* orientation = exif_content_get_entry(exif->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
        CHECK(orientation && exif_get_short(orientation->data, exif_data_get_byte_order(exif)) == 6);
        exif_data_unref(exif);
    }
    if (argc == 3) {
        printf("PASS native NV21 -> SuperShot -> Samsung JPEG %dx%d\n", w, h);
        return 0;
    }
    fake.drop = true;
    fake.captureEntered = false;
    CHECK(hdr.takePicture() == 0);
    for (int i = 0; i < 100 && !fake.captureEntered; ++i) std::this_thread::sleep_for(10ms);
    CHECK(fake.captureEntered);
    hdr.cancel();
    CHECK(fake.completed == 2 && fake.errors == 0);
    CHECK(fake.params.flatten().string() == fake.saved);
    fake.drop = false;
    fake.invalid = true;
    CHECK(hdr.takePicture() == 0);
    fake.wait(2, 1);
    fake.invalid = false;
    fake.shortFrame = true;
    CHECK(hdr.takePicture() == 0);
    fake.wait(2, 2);
    fake.shortFrame = false;
    fake.drop = true;
    CHECK(hdr.takePicture() == 0);
    fake.wait(2, 3);
    fake.drop = false;
    CHECK(hdr.takePicture() == 0);
    fake.wait(3, 3);
    // A callback may reenter camera APIs. Never join the current worker.
    fake.reenter = true;
    CHECK(hdr.takePicture() == 0);
    fake.wait(4, 3);
    fake.reenter = false;
    // Simulate CameraClient holding its lock while a completed JPEG callback
    // waits for that lock. cancel/stop_preview must return, not join callback.
    fake.blockCallback = true;
    fake.callbackEntered = false;
    CHECK(hdr.takePicture() == 0);
    for (int i = 0; i < 5000 && !fake.callbackEntered; ++i) std::this_thread::sleep_for(1ms);
    CHECK(fake.callbackEntered);
    auto before = std::chrono::steady_clock::now();
    hdr.cancel();
    CHECK(std::chrono::steady_clock::now() - before < 250ms);
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.releaseCallback = true;
    }
    fake.condition.notify_all();
    fake.wait(5, 3);
    fake.blockCallback = false;
    // Disabling the public JPEG callback must not disable collection of the
    // three internal NV21 buffers, or leak an unwanted result to the client.
    fake.drop = true;
    fake.captureEntered = false;
    CHECK(hdr.takePicture() == 0);
    for (int i = 0; i < 1000 && !fake.captureEntered; ++i) std::this_thread::sleep_for(1ms);
    CHECK(fake.captureEntered);
    hdr.disableMessages(CAMERA_MSG_COMPRESSED_IMAGE);
    CHECK(!hdr.messagesEnabled(CAMERA_MSG_COMPRESSED_IMAGE));
    CHECK(fake.messages & CAMERA_MSG_COMPRESSED_IMAGE);
    for (int i = 0; i < 3; ++i) {
        camera_memory_t frame = {};
        frame.data = fake.images[i].data(); frame.size = fake.images[i].size();
        CHECK(hdr.onData(CAMERA_MSG_COMPRESSED_IMAGE, &frame, 0));
    }
    for (int i = 0; i < 5000 && hdr.busy(); ++i) std::this_thread::sleep_for(1ms);
    CHECK(!hdr.busy() && fake.completed == 5 && fake.errors == 3);
    CHECK(!(fake.messages & CAMERA_MSG_COMPRESSED_IMAGE));
    // Two simultaneous capture calls must reserve only one native burst.
    std::atomic<bool> go{false};
    int results[2] = {};
    std::thread callers[2];
    for (int i = 0; i < 2; ++i) callers[i] = std::thread([&, i] {
        while (!go) std::this_thread::yield();
        results[i] = hdr.takePicture();
    });
    go = true;
    for (auto& caller : callers) caller.join();
    CHECK((results[0] == 0 && results[1] == -EBUSY) || (results[1] == 0 && results[0] == -EBUSY));
    hdr.cancel();
    fake.drop = false;
    hdr.enableMessages(CAMERA_MSG_COMPRESSED_IMAGE);
    CHECK(hdr.takePicture() == 0);
    fake.wait(6, 3);
    puts("PASS callback reentry, blocked delivery cancellation, message masks, concurrent capture");
    puts(
        "PASS native burst, OEM fusion/encoding, repeat, busy, parameters, cancel, invalid/short "
        "frame, timeout, recovery");
}
