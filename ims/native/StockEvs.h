// SPDX-License-Identifier: GPL-2.0
#pragma once
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <stdexcept>
// ARM32 ABI recovered from SM-A300FU A300FUXXU1CSG3 libevs_float.so.
// Keep private state opaque; the codec allocates/frees it through its own API.
struct EvsEncParams {
    int32_t bitrate = 13200, rate = 32000;
    int16_t* pcm = nullptr;
    uint8_t* bits = nullptr;
    int16_t options[10] = {};
};
struct EvsDecParams {
    int32_t rate = 32000;
    uint8_t* bits = nullptr;
    int16_t* pcm = nullptr;
    int16_t options[4] = {};
    int32_t bitrate = 13200;
    int16_t more[8] = {};
};
static_assert(sizeof(void*) == 4 && offsetof(EvsEncParams, options) == 16);
static_assert(offsetof(EvsDecParams, bitrate) == 20);
class StockEvs {
    void* library;
    void* encoder = nullptr;
    void* decoder = nullptr;
    void* (*createEnc)(EvsEncParams*);
    int (*encodeFrame)(void*, EvsEncParams*);
    void (*deleteEnc)(void*);
    void* (*createDec)(EvsDecParams*);
    int (*decodeFrame)(void*, EvsDecParams*);
    void (*deleteDec)(void*);
public:
    StockEvs() : library(dlopen("libevs_float.so", RTLD_NOW | RTLD_LOCAL)) {
        if (!library) throw std::runtime_error("Stock EVS library unavailable");
#define SYM(field, name) field = reinterpret_cast<decltype(field)>(dlsym(library, name))
        SYM(createEnc,"sEVSCreateEnc"); SYM(encodeFrame,"sEVSEncFrame"); SYM(deleteEnc,"sEVSDeleteEnc");
        SYM(createDec,"sEVSCreateDec"); SYM(decodeFrame,"sEVSDecFrame"); SYM(deleteDec,"sEVSDeleteDec");
#undef SYM
        if (!createEnc || !encodeFrame || !deleteEnc || !createDec || !decodeFrame || !deleteDec) {
            dlclose(library);
            throw std::runtime_error("Stock EVS ABI unavailable");
        }
    }
    ~StockEvs() {
        if (encoder) deleteEnc(encoder);
        if (decoder) deleteDec(decoder);
        dlclose(library);
    }
    StockEvs(const StockEvs&) = delete;
    StockEvs& operator=(const StockEvs&) = delete;
    // Fixed EVS Primary 13.2 kb/s SWB, 20 ms, no DTX, no channel-aware mode.
    void encode(int16_t* pcm, uint8_t* output) {
        EvsEncParams p;
        p.pcm = pcm; p.bits = output;
        p.options[5] = 2; // maximum encoded bandwidth: SWB
        if (!encoder) encoder = createEnc(&p);
        if (!encoder) throw std::runtime_error("EVS encoder allocation failed");
        if (encodeFrame(encoder, &p) != 34 || output[0] != 4)
            throw std::runtime_error("EVS encoder returned unexpected frame");
    }
    void decode(uint8_t* speech, int16_t* pcm) {
        EvsDecParams p;
        p.bits = speech; p.pcm = pcm;
        p.more[3] = 4; // EVS Primary FT 4 (13.2), not AMR-WB IO
        if (!decoder) decoder = createDec(&p);
        if (!decoder) throw std::runtime_error("EVS decoder allocation failed");
        if (decodeFrame(decoder, &p) != 0) throw std::runtime_error("EVS decode failed");
    }
};
