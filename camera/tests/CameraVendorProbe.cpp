/* SPDX-License-Identifier: Apache-2.0 */
#include <hardware/camera.h>
#include <hardware/hardware.h>
#include <camera/CameraParameters.h>
#include <utils/String8.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const hw_module_t* module = nullptr;
    int rc = hw_get_module_by_class("camera", "vendor", &module);
    if (rc) { fprintf(stderr, "module: %d\n", rc); return 1; }
    camera_device_t* camera = nullptr;
    rc = module->methods->open(module, "0", (hw_device_t**)&camera);
    if (rc) { fprintf(stderr, "open: %d\n", rc); return 1; }
    if (argc > 1 && !strcmp(argv[1], "--samsung")) {
        rc = camera->ops->send_command(camera, 1508, 0, 0);
        fprintf(stderr, "Samsung mode: %d\n", rc);
    }
    char* raw = camera->ops->get_parameters(camera);
    if (raw) {
        android::CameraParameters parameters;
        parameters.unflatten(android::String8(raw));
        printf("%s\n", raw);
        camera->ops->put_parameters(camera, raw);
    }
    camera->ops->release(camera);
    camera->common.close((hw_device_t*)camera);
    return rc ? 1 : 0;
}
