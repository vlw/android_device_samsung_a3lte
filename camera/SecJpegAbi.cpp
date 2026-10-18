/* SPDX-License-Identifier: Apache-2.0 */
#include "SecJpegAbi.h"

#include <dlfcn.h>
namespace android {
SecJpegEncoder::SecJpegEncoder() : mLibrary(nullptr), mParameters(nullptr) {}
SecJpegEncoder::~SecJpegEncoder() {}
int SecJpegEncoder::startEncodeSync(jpeg_encoder& parameters) {
    mParameters = &parameters;
    return encode();
}
int SecJpegEncoder::release(SecJpegEncoder* encoder) {
    if (!encoder) return -1;
    int rc = encoder->decref();
    if (!rc) {
        void* library = encoder->mLibrary;
        delete encoder;
        if (library) dlclose(library);
    }
    return rc;
}
}  // namespace android
