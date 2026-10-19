// SPDX-License-Identifier: GPL-2.0
#define LOG_TAG "A3ImsSrvcc"
#include "ims_srvcc.h"
#include <android/log.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <stdio.h>
#include <openssl/sha.h>
#include <time.h>
#include "srvcc_protocol.h"
#include "srvcc_state.h"

namespace {
// Audited 32-bit Samsung RIL ABI. All work touching CallManager is serialized
// by its EventScheduler, including list assignment and the subsequent request.
// There are no patches to executable code or existing vendor vtables.
struct HandlerStorage { void** vtable; int refs; uint32_t initialized; uint32_t id; };
struct MessageView {
    void* vtable; int refs; int kind; void* target;
    unsigned targetId; int event; void* data; int argument;
};
static_assert(sizeof(HandlerStorage) == 16, "32-bit RIL required");
static_assert(sizeof(MessageView) == 32, "32-bit RIL required");
using Parse = void* (*)(void*, char*, int, int&, int&);
using Create = void* (*)(void*, int, void*);
using Push = int (*)(void*, int, void*, void*, int, bool);
Parse parseList;
Create create;
Push push;
int (*sendMessage)(void*);
void* (*getCallManager)(void*);
void* (*assignList)(void*, const void*);
void (*makeState)(void*, int);
void** ril;
HandlerStorage* handler;
void* table[6];
void (*construct)(void*);
int (*initializeHandler)(void*);
void* (*scheduler)();
int (*addHandler)(void*, void*);
void** handlerVtable;
void* (*defaultProxy)(void*);
void (*observe)(void*, void*, int);
void (*notifySrvcc)(int, const void*, size_t);
int (*callCount)(void*);
SrvccTableFreeze freeze;
int64_t monotonicMs() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

bool auditedLibrary() {
    // Private member and scheduler ABI are intentionally restricted to this
    // exact shipped blob. A new RIL needs a new audit, never guessed offsets.
    constexpr uint8_t expected[32] = {
        0x36,0x89,0xee,0xe0,0xf1,0x5a,0xa3,0xe5,0xff,0x26,0x4a,0xb2,0x20,0xe5,0xee,0xea,
        0x07,0xc7,0x67,0x27,0x3c,0x0c,0x2a,0x72,0xd2,0xa8,0xba,0xcd,0x1f,0xf0,0x15,0x7c};
    FILE* file = fopen("/vendor/lib/libsec-ril.so", "rb");
    if (!file) return false;
    SHA256_CTX context;
    SHA256_Init(&context);
    uint8_t buffer[8192], hash[32];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file))) SHA256_Update(&context, buffer, bytes);
    bool ok = !ferror(file);
    fclose(file);
    SHA256_Final(hash, &context);
    return ok && memcmp(hash, expected, sizeof(hash)) == 0;
}

int handle(void*, MessageView* message) {
    if (!ril || !*ril) return -1;
    void* manager = getCallManager(*ril);
    if (!manager || !message->data) return -1;
    if (message->event == 2) {
        int state;
        memcpy(&state, static_cast<char*>(message->data) + 8, sizeof(state));
        if (freeze.onCpState(state, callCount(manager) > 0, monotonicMs())) {
            // Preserve the original STARTED/COMPLETED handling and also expose
            // the real CP failure/cancel that this CallManager otherwise omits.
            notifySrvcc(1039, &state, sizeof(state));
        }
        return 0;
    }
    if (freeze.blocksUpdates(monotonicMs())) return 0;
    // This is the only private member accessed: mVoLteCallList. Its offset is
    // checked against the exported DoSetVoLteCallList instruction below.
    void* list = nullptr;
    memcpy(&list, static_cast<char*>(manager) + 172, sizeof(list));
    if (!list) return -1;
    assignList(list, message->data);
    void* state = ::operator new(12);
    makeState(state, 4); // CALL_SRVCC_NONE: regular call-list configuration.
    // The normal worker owns state and handles IPC + optional QC CP config.
    int result = push(*ril, 1005, state, manager, 164, false);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
        "VoLTE call table updated; vendor call-config request result=%d", result);
    return result;
}
const char* name(void*) { return "A3ImsSrvcc"; }
}

bool initializeSamsungSrvcc(void* library, void** secRil,
        void (*notify)(int, const void*, size_t)) {
    if (!auditedLibrary()) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unaudited RIL; SRVCC bridge disabled");
        return false;
    }
    auto symbol = [library](const char* s) { return dlsym(library, s); };
    // Refuse an unaudited object layout. In the shipped legacy RIL this reads
    // mVoLteCallList into r2; the adjacent constructor/ABI symbols are required.
    auto method = reinterpret_cast<const uint8_t*>(
        reinterpret_cast<uintptr_t>(symbol("_ZN11CallManager18DoSetVoLteCallListEP7Request")) & ~uintptr_t(1));
    const uint8_t expected[] = {0xd4, 0xf8, 0xac, 0x20};
    if (!method || memcmp(method + 0x46, expected, sizeof(expected)) != 0 ||
        !symbol("_ZN8CallInfoC1E9CallStateiibbbPcS1_P11CallDetails16CallPresentationS4_cbP7UusInfobiiS1_S4_iS1_S4_bii")) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unsupported SRVCC vendor layout");
        return false;
    }
    parseList = reinterpret_cast<Parse>(symbol("_ZN13IpcProtocol4113IpcRxCallListEPciRiR14RegistrantType"));
    create = reinterpret_cast<Create>(symbol("_Z13CreateMessageP7HandleriP6Object"));
    sendMessage = reinterpret_cast<int (*)(void*)>(symbol("_Z11SendMessageP7Message"));
    push = reinterpret_cast<Push>(symbol("_ZN6SecRil19PushInternalRequestEiP7RilDataP7Handlerib"));
    getCallManager = reinterpret_cast<void* (*)(void*)>(symbol("_ZN6SecRil14GetCallManagerEv"));
    assignList = reinterpret_cast<void* (*)(void*, const void*)>(symbol("_ZN8CallListaSERKS_"));
    makeState = reinterpret_cast<void (*)(void*, int)>(symbol("_ZN10SrvccStateC1E14CallSrvccState"));
    construct = reinterpret_cast<void (*)(void*)>(symbol("_ZN7HandlerC1Ev"));
    initializeHandler = reinterpret_cast<int (*)(void*)>(symbol("_ZN7Handler10InitializeEv"));
    scheduler = reinterpret_cast<void* (*)()>(symbol("_ZN14EventScheduler11GetInstanceEv"));
    addHandler = reinterpret_cast<int (*)(void*, void*)>(symbol("_ZN14EventScheduler10AddHandlerEP7Handler"));
    handlerVtable = static_cast<void**>(symbol("_ZTV7Handler"));
    defaultProxy = reinterpret_cast<void* (*)(void*)>(symbol("_ZN6SecRil20GetDefaultModemProxyEv"));
    observe = reinterpret_cast<void (*)(void*, void*, int)>(symbol(
        "_ZN10ModemProxy32RegisterForCallSrvccNotificationEP7Handleri"));
    callCount = reinterpret_cast<int (*)(void*)>(symbol("_ZN11CallManager17GetVolteCallCountEv"));
    if (!parseList || !create || !sendMessage || !push || !getCallManager ||
        !assignList || !makeState || !construct || !initializeHandler || !scheduler || !addHandler || !handlerVtable ||
        !defaultProxy || !observe || !callCount || !notify)
        return false;
    ril = secRil;
    notifySrvcc = notify;
    // SecRil is initialized asynchronously on some restart paths. RIL_Init
    // must return before the vendor can finish that initialization.
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "SRVCC ABI ready; waiting for radio initialization");
    return true;
}

namespace {
bool ensureHandler() {
    if (handler) return true;
    if (!ril || !*ril || !getCallManager(*ril)) return false;
    void* proxy = defaultProxy(*ril);
    if (!proxy) return false;
    handler = new HandlerStorage{};
    construct(handler);
    memcpy(table, handlerVtable + 2, sizeof(table));
    table[4] = reinterpret_cast<void*>(handle);
    table[5] = reinterpret_cast<void*>(name);
    handler->vtable = table;
    if (initializeHandler(handler) != 0 || addHandler(scheduler(), handler) != 0) {
        reinterpret_cast<void (*)(void*)>(handler->vtable[1])(handler);
        handler = nullptr;
        return false;
    }
    observe(proxy, handler, 2);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "SRVCC call-table bridge ready");
    return true;
}
}

bool submitSamsungSrvcc(const uint8_t* body, size_t length, unsigned count) {
    if (!validateSrvccCalls(body, length, count) || !ensureHandler()) return false;
    // Use the vendor parser to construct all CallInfo/CallDetails objects.
    // The audited parser consumes the core voice fields; pad its diagnostic reads.
    char packet[1024]{};
    packet[0] = static_cast<char>(8 + length);
    packet[1] = static_cast<char>((8 + length) >> 8);
    packet[4] = 2; packet[5] = 6; packet[6] = 2; packet[7] = count;
    memcpy(packet + 8, body, length);
    int parsed = 0, registrant = 0;
    void* list = parseList(nullptr, packet, 8 + length, parsed, registrant);
    if (!list) return false;
    return sendMessage(create(handler, 1, list)) >= 0;
}
