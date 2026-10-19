// SPDX-License-Identifier: GPL-2.0
// Device-only adaptor for the msm8916-common Samsung RIL shipped with a3lte.
#define LOG_TAG "A3ImsRadio"
#include <telephony/ril.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
using Parse = void* (*)(void*, char*, int, int&, int&);
using Message = void* (*)(void*, int, void*);
using Send = int (*)(void*);
const RIL_RadioFunctions* vendor;
Parse parseRegistration, parsePreference;
Message makeMessage;
Send sendMessage;
bool modernIil = false;
void** manager;
void** secRil;
using GetProxy = void* (*)(void*, int, int);
using SetDomain = int (*)(void*, void*, int);
using SetNasRegistration = int (*)(void*, void*, void*);
GetProxy getProxy;
SetDomain setDomain;
SetNasRegistration setNasRegistration;

bool enqueue(Parse parse, char* packet, int length, int event) {
    int parsedLength = 0, registrant = 0;
    void* object = parse(nullptr, packet, length, parsedLength, registrant);
    if (!object) return false;
    bool queued = true;
    if (event == 200 && object && setNasRegistration) {
        // The default Samsung IPC IMS_REGI command and Qualcomm NAS IMS
        // registration status are separate interfaces. Update NAS through its
        // normal proxy too, before handing the parser-owned state to the IMS
        // worker. The asynchronous method encodes/copies the state on return.
        void* proxy = getProxy(*secRil, 4, 1);
        int status = proxy ? setNasRegistration(proxy, nullptr, object) : -1;
        queued = status >= 0;
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
            "NAS IMS registration notification queued result=%d", status);
    }
    // The vendor parser constructs its own Object. CreateMessage takes ownership;
    // event 200/201 is handled on ImsManager's existing worker, never this socket thread.
    sendMessage(makeMessage(*manager, event, object));
    return queued;
}

void* serve(void*) {
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) return nullptr;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    constexpr char name[] = "a3ims-radio";
    memcpy(address.sun_path + 1, name, sizeof(name) - 1);
    if (bind(server, reinterpret_cast<sockaddr*>(&address),
            offsetof(sockaddr_un, sun_path) + sizeof(name)) || listen(server, 4)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Cannot open state socket: %s", strerror(errno));
        close(server);
        return nullptr;
    }
    int previous = -1;
    for (;;) {
        int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) { if (errno == EINTR) continue; break; }
        timeval timeout{1, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ucred peer{};
        socklen_t length = sizeof(peer);
        uint8_t packet[8]{};
        uint8_t result = 1;
        char simState[PROP_VALUE_MAX]{};
        __system_property_get("gsm.sim.state", simState);
        const bool radioReady = strcmp(simState, "LOADED") == 0;
        if (!radioReady) previous = -1;
        if (radioReady && getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) == 0 &&
                (peer.uid == 1000 || peer.uid == 0) &&
                recv(client, packet, sizeof(packet), MSG_WAITALL) == sizeof(packet) &&
                memcmp(packet, "A3IM", 4) == 0 && packet[4] == 1 &&
                packet[5] <= 1 && packet[6] <= 3 && packet[7] == 0 &&
                (packet[5] || packet[6] == 0) && *manager) {
            int state = packet[5] | (packet[6] << 1);
            bool queued = true;
            if (state != previous) {
                // Decode the original legacy IIL wire format with the RIL's own
                // parser. No private C++ object layouts or text addresses are used.
                char reg[14] = {14, 0, 0, 0, 112, 1, 3,
                    static_cast<char>(packet[5]), static_cast<char>(packet[6]), 0, 0, 0, 0, 0};
                char modernReg[15] = {15, 0, 0, 0, 112, 1, 3,
                    static_cast<char>(packet[5]), static_cast<char>(packet[6]), 0,
                    static_cast<char>(((packet[6] & 1) ? 4 : 0) | (packet[6] & 2)), 0, 0, 0, 0};
                queued = enqueue(parseRegistration, modernIil ? modernReg : reg,
                    modernIil ? sizeof(modernReg) : sizeof(reg), 200);
                // 3GPP SMS, EUTRAN PS preferred (with CS fallback), UTRAN CS only,
                // SS/USSD/emergency CS, no ePDG/eHRPD/video. Enable IMS only when
                // the SIP service has actually registered.
                char pref[21] = {21, 0, 0, 0, 112, 6, 3,
                    0, static_cast<char>((packet[6] & 2) != 0), 0,
                    3, 0, 1, 1, 1, 1, 0, 0, 0,
                    static_cast<char>(packet[5]), 3};
                enqueue(parsePreference, pref, sizeof(pref), 201);
                // IIL preferences only update RIL's cache on this version. Send
                // its normal asynchronous NAS voice-domain request as well.
                // Like LteDataCallManager::DoSetVoiceDomainPref, no reply Message
                // is needed here. PS preferred retains CS fallback and keeps
                // LTE available while the AP is still registering.
                void* proxy = getProxy(*secRil, 4, 1);
                if (proxy) setDomain(proxy, nullptr, 3);
                // A transient NAS failure must be retried by the AP heartbeat;
                // an IPC-only update is insufficient for incoming VoLTE.
                if (queued) previous = state;
                __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                    "Queued Samsung IMS registration=%u capabilities=%u", packet[5], packet[6]);
            }
            result = queued ? 0 : 1;
        }
        send(client, &result, 1, MSG_NOSIGNAL);
        close(client);
    }
    close(server);
    return nullptr;
}
}

extern "C" const RIL_RadioFunctions* RIL_Init(const RIL_Env* env, int argc, char** argv) {
    void* library = dlopen("/vendor/lib/libsec-ril.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Cannot load Samsung RIL: %s", dlerror());
        return nullptr;
    }
    auto init = reinterpret_cast<decltype(&RIL_Init)>(dlsym(library, "RIL_Init"));
    if (!init) return nullptr;
    vendor = init(env, argc, argv);
    manager = reinterpret_cast<void**>(dlsym(library, "_ZN10ImsManager9mInstanceE"));
    parseRegistration = reinterpret_cast<Parse>(dlsym(library,
        "_ZN16IpcProtocol41Iil20IpcRxIilRegistrationEPciRiR14RegistrantType"));
    parsePreference = reinterpret_cast<Parse>(dlsym(library,
        "_ZN16IpcProtocol41Iil18IpcRxIilPreferenceEPciRiR14RegistrantType"));
    makeMessage = reinterpret_cast<Message>(dlsym(library, "_Z13CreateMessageP7HandleriP6Object"));
    sendMessage = reinterpret_cast<Send>(dlsym(library, "_Z11SendMessageP7Message"));
    secRil = reinterpret_cast<void**>(dlsym(library, "_ZN6SecRil9mInstanceE"));
    getProxy = reinterpret_cast<GetProxy>(dlsym(library, "_ZN6SecRil13GetModemProxyE9ModemTypeS0_"));
    setDomain = reinterpret_cast<SetDomain>(dlsym(library, "_ZN10ModemProxy14SetVoiceDomainEP7Message15VoiceDomainPref"));
    setNasRegistration = reinterpret_cast<SetNasRegistration>(dlsym(library,
        "_ZN10ModemProxy21SetImsRegStateChangedEP7MessageP11ImsRegState"));
    // Validate the exported constructor ABI before choosing the corresponding
    // IIL registration format; never feed an unknown parser a guessed layout.
    void* legacyAbi = dlsym(library,
        "_ZN11ImsRegStateC2E12ImsRegStatusc11NetworkType9EcmpState9EpdgState15SmsOverIpFormati");
    modernIil = dlsym(library,
        "_ZN11ImsRegStateC2E12ImsRegStatusc11NetworkTypec9EcmpState9EpdgState15SmsOverIpFormati") != nullptr;
    if (vendor && manager && secRil && (legacyAbi || modernIil) && getProxy && setDomain && setNasRegistration &&
            parseRegistration && parsePreference && makeMessage && sendMessage) {
        pthread_t thread;
        if (!pthread_create(&thread, nullptr, serve, nullptr)) pthread_detach(thread);
    } else {
        // All normal radio operations remain the original vendor implementation.
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Legacy IMS ABI unavailable; leaving radio unchanged");
    }
    return vendor;
}
