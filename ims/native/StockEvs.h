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
    int sampleRate;
    int encoderDtx = -1, encoderRf = -1;
    EvsEncParams encParams;
    EvsDecParams decParams;
    void* (*createEnc)(EvsEncParams*);
    int (*encodeFrame)(void*, EvsEncParams*);
    void (*deleteEnc)(void*);
    void* (*createDec)(EvsDecParams*);
    int (*decodeFrame)(void*, EvsDecParams*);
    void (*deleteDec)(void*);
public:
    StockEvs(int rate = 32000) : library(dlopen("libevs_float.so", RTLD_NOW | RTLD_LOCAL)), sampleRate(rate) {
        if (rate != 8000 && rate != 16000 && rate != 32000 && rate != 48000) throw std::runtime_error("Invalid EVS sample rate");
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
    int samples() const { return sampleRate / 50; }
    static int speechBits(uint8_t toc) {
        if (toc & 0xc0) return -1;
        static const int primary[] = {56,144,160,192,264,328,488,640,960,1280,1920,2560,48,-1,0,0};
        static const int io[] = {132,177,253,285,317,365,397,461,477,40,-1,-1,-1,-1,0,0};
        if (!(toc & 0x20) && (toc & 0x10)) return -1;
        return toc & 0x20 ? io[toc & 15] : primary[toc & 15];
    }
    int encode(int16_t* pcm, uint8_t* output, int bitrate, int bandwidth, bool dtx,
               int rfOffset = 0, bool rfHigh = true) {
        static const int primaryRates[] = {5900,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000};
        static const int ioRates[] = {6600,8850,12650,14250,15850,18250,19850,23050,23850};
        bool valid = false;
        for (int r : primaryRates) valid |= r == bitrate;
        for (int r : ioRates) valid |= r == bitrate;
        if (!valid || bandwidth < 0 || bandwidth > 3 ||
            (rfOffset != 0 && rfOffset != 2 && rfOffset != 3 && rfOffset != 5 && rfOffset != 7))
            throw std::runtime_error("Invalid EVS encoder configuration");
        bool io = false;
        for (int r : ioRates) io |= r == bitrate;
        if ((io && bandwidth != 1) || (!io && ((bandwidth==0 && bitrate>24400) ||
            (bandwidth==2 && bitrate<9600) || (bandwidth==3 && bitrate<16400))))
            throw std::runtime_error("EVS rate/bandwidth combination unsupported");
        if (rfOffset && (bitrate != 13200 || bandwidth < 1 || bandwidth > 2))
            throw std::runtime_error("Invalid EVS channel-aware configuration");
        if (encoder && (encoderDtx != dtx || encoderRf != (rfOffset != 0))) {
            deleteEnc(encoder); encoder = nullptr;
        }
        encParams.bitrate = bitrate; encParams.rate = sampleRate;
        encParams.pcm = pcm; encParams.bits = output;
        encParams.options[0] = dtx;
        encParams.options[2] = rfOffset != 0;
        encParams.options[3] = rfHigh;
        encParams.options[4] = rfOffset;
        encParams.options[5] = bandwidth;
        encParams.options[6] = 1; // apply bitrate changes, including Primary <-> IO
        if (!encoder) {
            encoder = createEnc(&encParams);
            encoderDtx = dtx; encoderRf = rfOffset != 0;
        }
        if (!encoder) throw std::runtime_error("EVS encoder allocation failed");
        const int size = encodeFrame(encoder, &encParams);
        const int bits = speechBits(output[0]);
        if (size < 1 || size > 321 || bits < 0 || size != 1 + (bits + 7) / 8)
            throw std::runtime_error("EVS encoder returned invalid frame");
        return size;
    }
    void decode(uint8_t* frame, int16_t* pcm) {
        static const int primary[] = {2800,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000,2400,0,0,0};
        static const int io[] = {6600,8850,12650,14250,15850,18250,19850,23050,23850,1750,0,0,0,0,0,0};
        const int ft = frame[0] & 15, mode = (frame[0] >> 5) & 1;
        decParams.rate = sampleRate; decParams.bits = frame + 1; decParams.pcm = pcm;
        decParams.bitrate = mode ? io[ft] : primary[ft];
        decParams.more[2] = mode; decParams.more[3] = ft;
        decParams.options[2] = ft == 14 || (mode && !(frame[0] & 0x10));
        if (!decoder) decoder = createDec(&decParams);
        if (!decoder) throw std::runtime_error("EVS decoder allocation failed");
        if (decodeFrame(decoder, &decParams) != 0) throw std::runtime_error("EVS decode failed");
    }
};
