// SPDX-License-Identifier: Apache-2.0
// Linkage diagnostic, with optional CDM capability query. No license requests.
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <media/drm/DrmAPI.h>
int main(int argc, char** argv) {
    if(argc!=2 && argc!=3) return 2;
    void* library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library) {printf("dlopen: %s\n",dlerror());return 1;}
    printf("loaded; createDrmFactory=%s\n",dlsym(library,"createDrmFactory")?"present":"absent");
    if(argc==3 && !strcmp(argv[2],"--query")) {
        // This legacy CDM can auto-import a factory keybox during initialization.
        // Capability diagnostics must never initiate that path.
        if(access("/factory/wv.keys",F_OK)==0) {
            puts("Refusing query with factory provisioning input present");
            dlclose(library);return 2;
        }
        auto create=reinterpret_cast<android::DrmFactory*(*)()>(dlsym(library,"createDrmFactory"));
        if(!create) {dlclose(library);return 1;}
        auto* factory=create();
        const uint8_t uuid[]={0xed,0xef,0x8b,0xa9,0x79,0xd6,0x4a,0xce,0xa3,0xc8,0x27,0xdc,0xd5,0x1d,0x21,0xed};
        android::DrmPlugin* plugin=nullptr;
        auto result=factory->createDrmPlugin(uuid,&plugin);
        printf("create plugin=%d\n",result);
        if(!result && plugin) {
            android::String8 level;
            auto status=plugin->getPropertyString(android::String8("securityLevel"),level);
            printf("securityLevel status=%d value=%s\n",status,level.c_str());
        }
        delete plugin;delete factory;
    }
    dlclose(library);return 0;
}
