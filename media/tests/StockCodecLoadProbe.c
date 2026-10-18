/* SPDX-License-Identifier: Apache-2.0 */
/* Isolated linker/stock OMX initialization probe. Never modifies the OMX service. */
#include <dlfcn.h>
#include <media/openmax/OMX_Component.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--component") == 0) {
        void *library = dlopen(argv[3], RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            printf("INIT LOAD FAIL %s\n", dlerror());
            return 1;
        }
        typedef OMX_ERRORTYPE (*Init)(OMX_HANDLETYPE, OMX_STRING);
        Init init = (Init)dlsym(library, "SEC_OMX_ComponentInit");
        if (!init) {
            printf("No SEC initializer\n");
            dlclose(library);
            return 1;
        }
        OMX_COMPONENTTYPE component;
        memset(&component, 0, sizeof(component));
        component.nSize = sizeof(component);
        // Samsung's stock SEC_OMX_Check_SizeVersion expects OMX 1.0, not AOSP 1.1.
        component.nVersion.s.nVersionMajor = 1;
        component.nVersion.s.nVersionMinor = 0;
        component.nVersion.s.nRevision = 0;
        OMX_ERRORTYPE error = init(&component, argv[2]);
        printf("INIT %s error=0x%x\n", argv[2], (unsigned)error);
        if (error != OMX_ErrorNone) {
            dlclose(library);
            return 1;
        }
        int port_failures = 0;
        for (unsigned port = 0; port < 2; ++port) {
            OMX_PARAM_PORTDEFINITIONTYPE def;
            memset(&def, 0, sizeof(def));
            def.nSize = sizeof(def);
            def.nVersion = component.nVersion;
            def.nPortIndex = port;
            error = component.GetParameter(&component, OMX_IndexParamPortDefinition, &def);
            if (error != OMX_ErrorNone) port_failures++;
            printf("PORT %u error=0x%x buffers=%u size=%u codec=%u color=%u\n", port,
                   (unsigned)error, (unsigned)def.nBufferCountActual, (unsigned)def.nBufferSize,
                   (unsigned)def.format.video.eCompressionFormat,
                   (unsigned)def.format.video.eColorFormat);
        }
        error = component.ComponentDeInit(&component);
        printf("DEINIT error=0x%x; no media buffers submitted\n", (unsigned)error);
        dlclose(library);
        return error == OMX_ErrorNone && !port_failures ? 0 : 1;
    }
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        void *library = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (library) {
            printf("LOAD PASS %s\n", argv[i]);
            dlclose(library);
        } else {
            printf("LOAD FAIL %s: %s\n", argv[i], dlerror());
            failures++;
        }
    }
    return failures ? 1 : 0;
}
