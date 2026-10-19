// SPDX-License-Identifier: GPL-2.0
// Offline vendor-parser ABI check: never calls RIL_Init or any modem operation.
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "srvcc_protocol.h"
using Parse = void* (*)(void*, char*, int, int&, int&);
int main() {
    void* lib = dlopen("/vendor/lib/libsec-ril.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr,"dlopen failed: %s\n",dlerror()); return 1; }
    auto feature = reinterpret_cast<void* (*)()>(dlsym(lib,"_ZN16SecNativeFeature11getInstanceEv"));
    auto enabled = reinterpret_cast<bool (*)(void*, const char*)>(dlsym(lib,
        "_ZN16SecNativeFeature15getEnableStatusEPKc"));
    auto integer = reinterpret_cast<int (*)(void*, const char*)>(dlsym(lib,
        "_ZN16SecNativeFeature10getIntegerEPKc"));
    if (!feature || !enabled || !integer) return 2;
    printf("CSC support=%d qcCallList=%d release=%d\n",
        enabled(feature(),"CscFeature_RIL_SupportSrvcc"),
        enabled(feature(),"CscFeature_RIL_SetVolteCalllistToQcCp"),
        integer(feature(),"CscFeature_RIL_SrvccRel"));
    auto parse = reinterpret_cast<Parse>(dlsym(lib,
        "_ZN13IpcProtocol4113IpcRxCallListEPciRiR14RegistrantType"));
    auto getType = reinterpret_cast<int (*)(void*)>(dlsym(lib,"_ZN11CallDetails11GetCallTypeEv"));
    if (!parse || !getType) return 2;
    for (int state=1; state<=6; ++state) {
        char frame[1024] = {22,0,0,0,2,6,2,1,5,1,1,1,static_cast<char>(state),0,6,33,
            '+','1','2','3','4','5'};
        if (!validateSrvccCalls(reinterpret_cast<uint8_t*>(frame+8),14,1)) return 3;
        int length=0, registrant=0;
        void* list = parse(nullptr,frame,22,length,registrant);
        if (!list) return 4;
        auto words = static_cast<uint32_t*>(list);
        auto call = reinterpret_cast<uint8_t*>(words[2]);
        if (words[3]!=1 || *reinterpret_cast<int*>(call+8)!=state-1 ||
            *reinterpret_cast<int*>(call+16)!=1 || call[25]!=0 ||
            getType(call+28)!=3 || *reinterpret_cast<int*>(call+12)!=-1 ||
            strcmp(reinterpret_cast<char*>(call+105),"+12345")) {
            printf("Parser mismatch: count=%u state=%d id=%d direction=%u type=%d numberMatches=%d\n",
                words[3], *reinterpret_cast<int*>(call+8), *reinterpret_cast<int*>(call+16),
                call[25], getType(call+28), strcmp(reinterpret_cast<char*>(call+105),"+12345")==0);
            return 5;
        }
        auto vtable = *static_cast<void***>(list);
        reinterpret_cast<void (*)(void*)>(vtable[1])(list);
    }
    puts("PASS stock parser: all six IPC states, CP ID, direction, voice type and number; no radio operations");
    return 0;
}
