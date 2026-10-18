/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
// A300FUXXU1CSG3 SecJpegEncoder ABI. This glue does not implement image processing;
// the unmodified stock libsecjpegboard performs conversion, padding and encoding.
namespace android {
struct jpeg_encoder {
    const uint8_t* input;
    int inputSize;
    void* reserved[2];
    int width, height, format;
    uint8_t* output;
    int outputCapacity, quality;
    void* progress;
    int outputSize;
};
static_assert(sizeof(jpeg_encoder) == 48, "Samsung 32-bit JPEG ABI");
class SecJpegEncoder {
public:
    SecJpegEncoder();
    virtual int type() const = 0;
    virtual const char* name() const = 0;
    virtual ~SecJpegEncoder();
    virtual int decref() = 0;
    virtual int encode() = 0;
    int startEncodeSync(jpeg_encoder& parameters);
    static int release(SecJpegEncoder* encoder);

protected:
    void* mLibrary;
    jpeg_encoder* mParameters;
};
static_assert(sizeof(SecJpegEncoder) == 12, "Samsung 32-bit encoder base ABI");
}  // namespace android
