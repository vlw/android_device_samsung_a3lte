/* SPDX-License-Identifier: Apache-2.0 */
#define LOG_TAG "A3StockJpeg"
#include "StockJpeg.h"

#include <dlfcn.h>
#include <log/log.h>
#include <stdlib.h>

#include <algorithm>

#include "SecJpegAbi.h"

StockJpeg::StockJpeg()
    : mBoard(dlopen("/vendor/lib/libsecjpegboard.so", RTLD_NOW | RTLD_LOCAL)),
      mQuram(dlopen("/vendor/lib/libQjpeg.so", RTLD_NOW | RTLD_LOCAL)) {
    if (mBoard && !dlsym(mBoard, "create_SecJpegEncoder")) {
        dlclose(mBoard);
        mBoard = nullptr;
    }
    if (mQuram && !dlsym(mQuram, "encodeJPEGToBuffer")) {
        dlclose(mQuram);
        mQuram = nullptr;
    }
    if (!mBoard) ALOGE("Samsung JPEG board encoder unavailable: %s", dlerror());
}
StockJpeg::~StockJpeg() {
    if (mBoard) dlclose(mBoard);
    if (mQuram) dlclose(mQuram);
}
bool StockJpeg::encode(const uint8_t* nv21, int width, int height, int quality,
                       std::vector<uint8_t>* jpeg) {
    if (!jpeg || !mBoard || !nv21 || width < 64 || height < 64 || width > 3264 || height > 3264 ||
        (width & 1) || (height & 1) || quality < 1 || quality > 100)
        return false;
    jpeg->assign(width * height * 3 + 65536, 0);
    android::jpeg_encoder job = {nv21,
                                 width * height * 3 / 2,
                                 {nullptr, nullptr},
                                 width,
                                 height,
                                 2,
                                 jpeg->data(),
                                 static_cast<int>(jpeg->size()),
                                 quality,
                                 nullptr,
                                 0};
    using Factory = android::SecJpegEncoder* (*)(android::jpeg_encoder*);
    auto create = reinterpret_cast<Factory>(dlsym(mBoard, "create_SecJpegEncoder"));
    auto* encoder = create(&job);
    int rc = -1;
    if (encoder) {
        rc = encoder->startEncodeSync(job);
        ALOGI("Samsung %s JPEG: %dx%d quality=%d status=%d size=%d", encoder->name(), width, height,
              quality, rc, job.outputSize);
        android::SecJpegEncoder::release(encoder);
    }
    // The stock factory tries hardware first, then Quram. Use that exact OEM
    // software encoder on hardware failure; never substitute AOSP libjpeg.
    if ((rc || job.outputSize < 4) && mQuram) {
        using Quram = int (*)(const uint8_t*, int, int, int, uint8_t**, int*, int);
        uint8_t* output = jpeg->data();
        int capacity = jpeg->size();
        rc = reinterpret_cast<Quram>(dlsym(mQuram, "encodeJPEGToBuffer"))(
            nv21, width, height, 18, &output, &capacity, quality);
        // The OEM API receives an existing output buffer in stock as well.
        if (output != jpeg->data()) {
            if (output && capacity >= 4 && capacity <= width * height * 3 + 65536) {
                jpeg->assign(output, output + capacity);
                free(output);
                return (*jpeg)[0] == 0xff && (*jpeg)[1] == 0xd8;
            }
            free(output);
            return false;
        }
        job.outputSize = capacity;
        // SecJpegQuramEnc ignores the C codec return and validates output size.
        rc = 0;
    }
    if (rc || job.outputSize < 4 || job.outputSize > static_cast<int>(jpeg->size()) ||
        (*jpeg)[0] != 0xff || (*jpeg)[1] != 0xd8)
        return false;
    jpeg->resize(job.outputSize);
    return true;
}
