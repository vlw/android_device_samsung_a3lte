// SPDX-License-Identifier: GPL-2.0
// Reads Samsung CSC features through the stock libsecnativefeature C API.
// Usage: a3_csc_feature_probe CscFeature_Name [...]
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char** argv) {
    void* lib = dlopen("libsecnativefeature.so", RTLD_NOW);
    if (!lib) {
        printf("FAIL dlopen: %s\n", dlerror());
        return 1;
    }
    int (*enabled)(const char*) = (int (*)(const char*))dlsym(lib, "SecNativeFeature_getEnableStatus");
    const char* (*string)(const char*) = (const char* (*)(const char*))dlsym(lib, "SecNativeFeature_getString");
    if (!enabled || !string) {
        printf("FAIL dlsym\n");
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        const char* value = string(argv[i]);
        printf("%s enabled=%d value=%s\n", argv[i], enabled(argv[i]), value ? value : "(null)");
    }
    return 0;
}
