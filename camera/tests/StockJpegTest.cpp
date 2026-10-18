/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>

#include <jpeglib.h>
#include <stdlib.h>

#include "../StockJpeg.h"
int main(int argc, char** argv) {
    int w = argc == 3 ? atoi(argv[1]) : 640, h = argc == 3 ? atoi(argv[2]) : 480;
    if (w < 64 || h < 64 || w > 3264 || h > 3264 || (w & 1) || (h & 1)) return 1;
    StockJpeg encoder;
    if (!encoder.available()) return 2;
    std::vector<uint8_t> nv21(w * h * 3 / 2, 128), jpeg;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) nv21[y * w + x] = 16 + x * 220 / w;
    if (!encoder.encode(nv21.data(), w, h, 96, &jpeg)) return 3;
    jpeg_decompress_struct dec = {};
    jpeg_error_mgr error;
    dec.err = jpeg_std_error(&error);
    jpeg_create_decompress(&dec);
    jpeg_mem_src(&dec, jpeg.data(), jpeg.size());
    jpeg_read_header(&dec, TRUE);
    bool valid =
        dec.image_width == static_cast<unsigned>(w) && dec.image_height == static_cast<unsigned>(h);
    jpeg_destroy_decompress(&dec);
    printf("Samsung JPEG %dx%d: %zu bytes %s\n", w, h, jpeg.size(), valid ? "PASS" : "FAIL");
    return valid ? 0 : 4;
}
