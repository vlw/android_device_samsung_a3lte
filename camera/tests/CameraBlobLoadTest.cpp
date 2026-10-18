/* SPDX-License-Identifier: Apache-2.0 */
// ELF load test; --module also queries module information and camera count.
// No camera device is opened and no sensor frames are captured.
#include <dlfcn.h>
#include <stdio.h>
#include <hardware/camera.h>
#include <string.h>
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    int failures=0;
    bool moduleTest = !strcmp(argv[1], "--module");
    for (int i=moduleTest ? 2 : 1;i<argc;++i) {
        void* library=dlopen(argv[i],RTLD_NOW|RTLD_LOCAL);
        if (!library) {fprintf(stderr,"FAIL %s: %s\n",argv[i],dlerror());++failures;}
        else {
            printf("PASS dlopen %s\n",argv[i]); fflush(stdout);
            if (moduleTest) {
                auto* module = reinterpret_cast<camera_module_t*>(dlsym(library, HAL_MODULE_INFO_SYM_AS_STR));
                if (!module) {++failures;}
                else {
                    printf("module_version=%x\n", module->common.module_api_version); fflush(stdout);
                    printf("camera_count=%d\n",module->get_number_of_cameras()); fflush(stdout);
                    if (module->common.module_api_version < CAMERA_MODULE_API_VERSION_2_0) {
                        for (int camera=0;camera<2;++camera) {
                            camera_info info={};int rc=module->get_camera_info(camera,&info);
                            printf("camera %d rc=%d facing=%d orientation=%d\n",camera,rc,info.facing,info.orientation);
                        }
                    }
                }
            }
            dlclose(library);
        }
    }
    return failures?1:0;
}
