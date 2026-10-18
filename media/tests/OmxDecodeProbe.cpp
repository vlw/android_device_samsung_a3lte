// SPDX-License-Identifier: Apache-2.0
// Isolated byte-buffer OMX AVC comparison. Never replaces the system OMX service.
#include <OMX_Component.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct Context {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<OMX_BUFFERHEADERTYPE*> input, output;
    OMX_STATETYPE state = OMX_StateLoaded;
    OMX_U32 error = 0;
    bool portChanged = false, disabled = false, enabled = true;
};
static OMX_ERRORTYPE event(OMX_HANDLETYPE, OMX_PTR data, OMX_EVENTTYPE type, OMX_U32 a, OMX_U32 b,
                           OMX_PTR) {
    auto& c = *static_cast<Context*>(data);
    std::lock_guard<std::mutex> lock(c.mutex);
    if (type == OMX_EventError) c.error = a;
    if (type == OMX_EventCmdComplete && a == OMX_CommandStateSet)
        c.state = static_cast<OMX_STATETYPE>(b);
    if (type == OMX_EventPortSettingsChanged && a == 1 && b != OMX_IndexConfigCommonOutputCrop)
        c.portChanged = true;
    if (type == OMX_EventCmdComplete && a == OMX_CommandPortDisable && b == 1) c.disabled = true;
    if (type == OMX_EventCmdComplete && a == OMX_CommandPortEnable && b == 1) c.enabled = true;
    c.changed.notify_all();
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE empty(OMX_HANDLETYPE, OMX_PTR data, OMX_BUFFERHEADERTYPE* buffer) {
    auto& c = *static_cast<Context*>(data);
    std::lock_guard<std::mutex> lock(c.mutex);
    c.input.push_back(buffer);
    c.changed.notify_all();
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE filled(OMX_HANDLETYPE, OMX_PTR data, OMX_BUFFERHEADERTYPE* buffer) {
    auto& c = *static_cast<Context*>(data);
    std::lock_guard<std::mutex> lock(c.mutex);
    c.output.push_back(buffer);
    c.changed.notify_all();
    return OMX_ErrorNone;
}
static void check(OMX_ERRORTYPE result, const char* action) {
    if (result != OMX_ErrorNone) {
        fprintf(stderr, "FAIL %s error=0x%x\n", action, static_cast<unsigned>(result));
        _Exit(2);
    }
}
template <typename T>
static T header() {
    T value{};
    value.nSize = sizeof(value);
    value.nVersion.s.nVersionMajor = 1;
    value.nVersion.s.nVersionMinor = 1;
    value.nVersion.s.nRevision = 2;
    return value;
}
static void waitState(Context& c, OMX_STATETYPE state) {
    std::unique_lock<std::mutex> lock(c.mutex);
    bool ready = c.changed.wait_for(lock, std::chrono::seconds(10),
                                    [&] { return c.error || c.state == state; });
    if (!ready || c.error) {
        fprintf(stderr, "FAIL state=%d wanted=%d error=0x%x\n", c.state, state, c.error);
        _Exit(3);
    }
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 5) {
        fprintf(stderr, "CORE_LIBRARY AVC_MP4 EXPECTED_FRAMES OUTPUT_PREFIX\n");
        return 1;
    }
    int expected = atoi(argv[3]);
    int fd = open(argv[2], O_RDONLY);
    if (fd < 0) return 1;
    off_t bytes = lseek(fd, 0, SEEK_END);
    auto* extractor = AMediaExtractor_new();
    if (AMediaExtractor_setDataSourceFd(extractor, fd, 0, bytes) != AMEDIA_OK) return 1;
    AMediaFormat* format = nullptr;
    for (size_t i = 0; i < AMediaExtractor_getTrackCount(extractor); ++i) {
        auto* candidate = AMediaExtractor_getTrackFormat(extractor, i);
        const char* mime = nullptr;
        if (AMediaFormat_getString(candidate, AMEDIAFORMAT_KEY_MIME, &mime) &&
            strcmp(mime, "video/avc") == 0) {
            format = candidate;
            AMediaExtractor_selectTrack(extractor, i);
            break;
        }
        AMediaFormat_delete(candidate);
    }
    if (!format) return 1;
    int32_t width = 0, height = 0;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height);
    void* core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        fprintf(stderr, "FAIL load: %s\n", dlerror());
        return 1;
    }
    auto init = reinterpret_cast<OMX_ERRORTYPE (*)()>(dlsym(core, "OMX_Init"));
    auto deinit = reinterpret_cast<OMX_ERRORTYPE (*)()>(dlsym(core, "OMX_Deinit"));
    auto get = reinterpret_cast<OMX_ERRORTYPE (*)(OMX_HANDLETYPE*, OMX_STRING, OMX_PTR,
                                                  OMX_CALLBACKTYPE*)>(dlsym(core, "OMX_GetHandle"));
    auto release =
        reinterpret_cast<OMX_ERRORTYPE (*)(OMX_HANDLETYPE)>(dlsym(core, "OMX_FreeHandle"));
    if (!init || !deinit || !get || !release) return 1;
    check(init(), "OMX_Init");
    Context c;
    OMX_CALLBACKTYPE callbacks{event, empty, filled};
    OMX_HANDLETYPE handle = nullptr;
    char name[] = "OMX.qcom.video.decoder.avc";
    check(get(&handle, name, &c, &callbacks), "OMX_GetHandle");
    auto* codec = static_cast<OMX_COMPONENTTYPE*>(handle);
    auto in = header<OMX_PARAM_PORTDEFINITIONTYPE>();
    in.nPortIndex = 0;
    check(codec->GetParameter(handle, OMX_IndexParamPortDefinition, &in), "get input");
    in.format.video.nFrameWidth = width;
    in.format.video.nFrameHeight = height;
    check(codec->SetParameter(handle, OMX_IndexParamPortDefinition, &in), "set input");
    auto out = header<OMX_PARAM_PORTDEFINITIONTYPE>();
    out.nPortIndex = 1;
    check(codec->GetParameter(handle, OMX_IndexParamPortDefinition, &out), "get output");
    out.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    check(codec->SetParameter(handle, OMX_IndexParamPortDefinition, &out), "set output");
    check(codec->GetParameter(handle, OMX_IndexParamPortDefinition, &in), "input allocation");
    check(codec->GetParameter(handle, OMX_IndexParamPortDefinition, &out), "output allocation");
    printf("ALLOC %dx%d input=%u/%u output=%u/%u color=%u stride=%d slice=%u\n", width, height,
           in.nBufferCountActual, in.nBufferSize, out.nBufferCountActual, out.nBufferSize,
           out.format.video.eColorFormat, out.format.video.nStride, out.format.video.nSliceHeight);
    check(codec->SendCommand(handle, OMX_CommandStateSet, OMX_StateIdle, nullptr), "idle");
    std::vector<OMX_BUFFERHEADERTYPE*> input, output;
    for (OMX_U32 i = 0; i < in.nBufferCountActual; ++i) {
        OMX_BUFFERHEADERTYPE* b = nullptr;
        check(codec->AllocateBuffer(handle, &b, 0, nullptr, in.nBufferSize), "allocate input");
        input.push_back(b);
        c.input.push_back(b);
    }
    for (OMX_U32 i = 0; i < out.nBufferCountActual; ++i) {
        OMX_BUFFERHEADERTYPE* b = nullptr;
        check(codec->AllocateBuffer(handle, &b, 1, nullptr, out.nBufferSize), "allocate output");
        output.push_back(b);
    }
    waitState(c, OMX_StateIdle);
    check(codec->SendCommand(handle, OMX_CommandStateSet, OMX_StateExecuting, nullptr), "execute");
    waitState(c, OMX_StateExecuting);
    for (auto* b : output) check(codec->FillThisBuffer(handle, b), "fill initial");
    std::vector<std::vector<unsigned char>> config;
    for (int i = 0;; ++i) {
        std::string key = "csd-" + std::to_string(i);
        void* p = nullptr;
        size_t n = 0;
        if (!AMediaFormat_getBuffer(format, key.c_str(), &p, &n)) break;
        config.emplace_back(static_cast<unsigned char*>(p), static_cast<unsigned char*>(p) + n);
    }
    size_t nextConfig = 0;
    bool inEos = false, outEos = false;
    int frames = 0;
    int64_t lastPts = -1;
    auto start = std::chrono::steady_clock::now();
    while (!outEos && std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
        bool resize = false;
        {
            std::lock_guard<std::mutex> lock(c.mutex);
            resize = c.portChanged;
            c.portChanged = false;
        }
        if (resize) {
            printf("RECONFIG output port\n");
            {
                std::lock_guard<std::mutex> lock(c.mutex);
                c.disabled = false;
            }
            check(codec->SendCommand(handle, OMX_CommandPortDisable, 1, nullptr), "disable output");
            {
                std::unique_lock<std::mutex> lock(c.mutex);
                bool ready = c.changed.wait_for(lock, std::chrono::seconds(10), [&] {
                    return c.error || c.output.size() == output.size();
                });
                if (!ready || c.error) {
                    fprintf(stderr, "FAIL recalling output buffers %zu/%zu\n", c.output.size(),
                            output.size());
                    _Exit(6);
                }
                c.output.clear();
            }
            for (auto* b : output) check(codec->FreeBuffer(handle, 1, b), "free old output");
            output.clear();
            {
                std::unique_lock<std::mutex> lock(c.mutex);
                if (!c.changed.wait_for(lock, std::chrono::seconds(10), [&] { return c.disabled; }))
                    _Exit(6);
            }
            check(codec->GetParameter(handle, OMX_IndexParamPortDefinition, &out), "new output");
            {
                std::lock_guard<std::mutex> lock(c.mutex);
                c.enabled = false;
            }
            check(codec->SendCommand(handle, OMX_CommandPortEnable, 1, nullptr), "enable output");
            for (OMX_U32 i = 0; i < out.nBufferCountActual; ++i) {
                OMX_BUFFERHEADERTYPE* b = nullptr;
                check(codec->AllocateBuffer(handle, &b, 1, nullptr, out.nBufferSize),
                      "allocate new output");
                output.push_back(b);
            }
            {
                std::unique_lock<std::mutex> lock(c.mutex);
                if (!c.changed.wait_for(lock, std::chrono::seconds(10), [&] { return c.enabled; }))
                    _Exit(6);
            }
            for (auto* b : output) check(codec->FillThisBuffer(handle, b), "fill new output");
        }
        OMX_BUFFERHEADERTYPE *ib = nullptr, *ob = nullptr;
        {
            std::unique_lock<std::mutex> lock(c.mutex);
            c.changed.wait_for(lock, std::chrono::milliseconds(2), [&] {
                return c.error || !c.output.empty() || (!inEos && !c.input.empty());
            });
            if (c.error) check(static_cast<OMX_ERRORTYPE>(c.error), "callback");
            if (!c.output.empty()) {
                ob = c.output.front();
                c.output.pop_front();
            }
            if (!inEos && !c.input.empty()) {
                ib = c.input.front();
                c.input.pop_front();
            }
        }
        if (ob) {
            if (ob->nFilledLen && !(ob->nFlags & OMX_BUFFERFLAG_CODECCONFIG)) {
                if (ob->nTimeStamp < lastPts) {
                    fprintf(stderr, "FAIL reordered PTS\n");
                    _Exit(4);
                }
                lastPts = ob->nTimeStamp;
                if (!frames) {
                    std::string path = std::string(argv[4]) + ".first.bin";
                    FILE* f = fopen(path.c_str(), "wb");
                    if (!f) return 1;
                    fwrite(ob->pBuffer + ob->nOffset, 1, ob->nFilledLen, f);
                    fclose(f);
                    check(codec->GetParameter(handle, OMX_IndexParamPortDefinition, &out),
                          "actual output");
                    printf(
                        "OUTPUT width=%u height=%u stride=%d slice=%u color=%u bytes=%u pts=%lld\n",
                        out.format.video.nFrameWidth, out.format.video.nFrameHeight,
                        out.format.video.nStride, out.format.video.nSliceHeight,
                        out.format.video.eColorFormat, ob->nFilledLen, (long long)ob->nTimeStamp);
                }
                ++frames;
            }
            outEos = ob->nFlags & OMX_BUFFERFLAG_EOS;
            if (!outEos) check(codec->FillThisBuffer(handle, ob), "refill");
        }
        if (ib) {
            ib->nOffset = 0;
            ib->nFilledLen = 0;
            ib->nFlags = 0;
            ib->nTimeStamp = 0;
            if (nextConfig < config.size()) {
                auto& data = config[nextConfig++];
                if (data.size() > ib->nAllocLen) return 1;
                memcpy(ib->pBuffer, data.data(), data.size());
                ib->nFilledLen = data.size();
                ib->nFlags = OMX_BUFFERFLAG_CODECCONFIG;
            } else {
                ssize_t n = AMediaExtractor_readSampleData(extractor, ib->pBuffer, ib->nAllocLen);
                if (n < 0) {
                    ib->nFlags = OMX_BUFFERFLAG_EOS;
                    inEos = true;
                } else {
                    ib->nFilledLen = n;
                    ib->nTimeStamp = AMediaExtractor_getSampleTime(extractor);
                    ib->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;
                    AMediaExtractor_advance(extractor);
                }
            }
            check(codec->EmptyThisBuffer(handle, ib), "empty input");
        }
    }
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("%s frames=%d expected=%d eos=%d fps=%.2f\n",
           outEos && frames == expected ? "PASS" : "FAIL", frames, expected, outEos,
           frames / seconds);
    fflush(stdout);
    if (!outEos || frames != expected) _Exit(5);
    check(codec->SendCommand(handle, OMX_CommandStateSet, OMX_StateIdle, nullptr), "finish idle");
    waitState(c, OMX_StateIdle);
    check(codec->SendCommand(handle, OMX_CommandStateSet, OMX_StateLoaded, nullptr),
          "finish loaded");
    for (auto* b : input) check(codec->FreeBuffer(handle, 0, b), "free input");
    for (auto* b : output) check(codec->FreeBuffer(handle, 1, b), "free output");
    waitState(c, OMX_StateLoaded);
    check(release(handle), "release");
    check(deinit(), "deinit");
    AMediaFormat_delete(format);
    AMediaExtractor_delete(extractor);
    close(fd);
    dlclose(core);
    return 0;
}
