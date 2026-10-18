/*
 * Copyright (C) 2014, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file CameraWrapper.cpp
*
* This file wraps a vendor camera module.
*
*/

#define LOG_NDEBUG 1

#define LOG_TAG "CameraWrapper"
#include <log/log.h>

#include <utils/threads.h>
#include <utils/String8.h>
#include <hardware/hardware.h>
#include <hardware/camera.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
#include <cutils/properties.h>
#include <string>
#include <atomic>
#include <memory>
#include <new>
#include "StockHdr.h"

static android::Mutex gCameraWrapperLock;
static android::Mutex gVendorModuleLock;
static camera_module_t *gVendorModule = 0;

static int camera_device_open(const hw_module_t *module, const char *name,
        hw_device_t **device);
static int camera_get_number_of_cameras(void);
static int camera_set_parameters(camera_device_t* device, const char* params);
static int camera_module_set_callbacks(const camera_module_callbacks_t *callbacks);
static int camera_module_set_torch(const char *id, bool enabled);
static int camera_get_camera_info(int camera_id, struct camera_info *info);
static int camera_send_command(struct camera_device *device, int32_t cmd,
        int32_t arg1, int32_t arg2);

static struct hw_module_methods_t camera_module_methods = {
    .open = camera_device_open,
};

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
         .hal_api_version = HARDWARE_HAL_API_VERSION,
         .id = CAMERA_HARDWARE_MODULE_ID,
         .name = "Samsung MSM8916 Camera Wrapper",
         .author = "The CyanogenMod Project",
         .methods = &camera_module_methods,
         .dso = NULL, /* remove compilation warnings */
         .reserved = {0}, /* remove compilation warnings */
    },

    .get_number_of_cameras = camera_get_number_of_cameras,
    .get_camera_info = camera_get_camera_info,
    .set_callbacks = camera_module_set_callbacks,
    .get_vendor_tag_ops = NULL, /* remove compilation warnings */
    .open_legacy = NULL, /* remove compilation warnings */
    .set_torch_mode = camera_module_set_torch,
    .init = NULL, /* remove compilation warnings */
    .reserved = {0}, /* remove compilation warnings */
};

typedef struct wrapper_camera_device {
    camera_device_t base;
    int camera_released;
    int id;
    camera_device_t *vendor;
    camera_notify_callback notify;
    camera_data_callback data;
    camera_data_timestamp_callback timestamp;
    camera_request_memory memory;
    void *user;
    StockHdr *hdr;
    std::atomic<bool> hdr_selected{false};
} wrapper_camera_device_t;

// Samsung's stream allocator passes QCameraStreamMemory as the memory callback
// cookie, not the cookie supplied to set_callbacks. Bind memory requests through
// separate trampolines for this device's rear and front cameras instead.
static wrapper_camera_device_t *gOpenDevices[2] = {NULL, NULL};

#define VENDOR_CALL(device, func, ...) ({ \
    wrapper_camera_device_t *__wrapper_dev = (wrapper_camera_device_t*) device; \
    __wrapper_dev->vendor->ops->func(__wrapper_dev->vendor, ##__VA_ARGS__); \
})

static int check_vendor_module()
{
    int rv = 0;
    android::Mutex::Autolock lock(gVendorModuleLock);
    ALOGV("%s", __FUNCTION__);

    if (gVendorModule)
        return 0;

    rv = hw_get_module_by_class("camera", "vendor",
            (const hw_module_t**)&gVendorModule);
    if (rv)
        ALOGE("failed to open vendor camera module");
    return rv;
}

#define KEY_VIDEO_HFR_VALUES "video-hfr-values"
static char *camera_fixup_getparams(wrapper_camera_device_t *wrapper, const char *settings)
{
    android::CameraParameters params;
    params.unflatten(android::String8(settings));

#if !LOG_NDEBUG && defined(LOG_PARAMETERS)
    ALOGV("%s: original parameters:", __FUNCTION__);
    params.dump();
#endif

    // Keep the vendor's real EV units, resolutions and FPS ranges. The sensor
    // uses 0.1 EV (-20..20), not the previously advertised 0.5 EV (-2..2).

    /* If the vendor has HFR values but doesn't also expose that
     * this can be turned off, fixup the params to tell the Camera
     * that it really is okay to turn it off.
     */
    params.set(KEY_VIDEO_HFR_VALUES, "off");

    if (wrapper->hdr && wrapper->hdr->available()) {
        const char *scenes = params.get(android::CameraParameters::KEY_SUPPORTED_SCENE_MODES);
        if (scenes) {
            std::string list(scenes);
            if (("," + list + ",").find(",hdr,") == std::string::npos)
                params.set(android::CameraParameters::KEY_SUPPORTED_SCENE_MODES,
                           (list + ",hdr").c_str());
        }
    }

    if (wrapper->hdr_selected) {
        params.set(android::CameraParameters::KEY_SCENE_MODE, "hdr");
        params.set(android::CameraParameters::KEY_PICTURE_FORMAT, "jpeg");
    }

    android::String8 strParams = params.flatten();
    char *ret = strdup(strParams.string());

#if !LOG_NDEBUG
    ALOGV("%s: fixed parameters:", __FUNCTION__);
    params.dump();
#endif

    return ret;
}

static char *camera_fixup_setparams(struct camera_device *device, const char *settings)
{
    wrapper_camera_device_t *wrapper = (wrapper_camera_device_t *)device;
    android::CameraParameters params;
    params.unflatten(android::String8(settings));

#if !LOG_NDEBUG
    ALOGV("%s: original parameters:", __FUNCTION__);
    params.dump();
#endif

    const char *scene = params.get(android::CameraParameters::KEY_SCENE_MODE);
    const char* recordingHint = params.get(android::CameraParameters::KEY_RECORDING_HINT);
    bool video = recordingHint && !strcmp(recordingHint, "true");
    bool requestedHdr = scene && !strcmp(scene, "hdr");
    if (requestedHdr && !video) {
        if (!wrapper->hdr || !wrapper->hdr->available()) return NULL;
        params.set(android::CameraParameters::KEY_SCENE_MODE, "auto");
        params.set(android::CameraParameters::KEY_FLASH_MODE, "off");
        params.set("zsl", "off");
        params.set("hdr-mode", 1);
        params.set("hdr-need-1x", "false");
        params.set("ae-bracket-hdr", "Off");
        params.set("num-snaps-per-shutter", 1);
    } else if (wrapper->hdr_selected || (requestedHdr && video)) {
        if (requestedHdr) params.set(android::CameraParameters::KEY_SCENE_MODE, "auto");
        params.set(android::CameraParameters::KEY_PICTURE_FORMAT, "jpeg");
        params.set("hdr-mode", 0);
        params.set("ae-bracket-hdr", "Off");
        params.remove("capture-burst-exposures");
        params.set("num-snaps-per-shutter", 1);
    }

    android::String8 strParams = params.flatten();

    char *ret = strdup(strParams.string());
    if (!ret) return NULL;

#if !LOG_NDEBUG
    ALOGV("%s: fixed parameters:", __FUNCTION__);
    params.dump();
#endif

    return ret;
}

/*******************************************************************
 * implementation of camera_device_ops functions
 *******************************************************************/

static int camera_set_preview_window(struct camera_device *device,
        struct preview_stream_ops *window)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    if (wrapper->hdr && wrapper->hdr->active()) return -EBUSY;
    return VENDOR_CALL(device, set_preview_window, window);
}

void camera_notify_cb(int32_t msg_type, int32_t ext1, int32_t ext2, void *user) {
    wrapper_camera_device_t *d = (wrapper_camera_device_t *)user;
    if (d->hdr && d->hdr->onNotify(msg_type)) return;
    if (d->hdr_selected && msg_type == CAMERA_MSG_SHUTTER &&
        (!d->hdr->active() || !d->hdr->messagesEnabled(msg_type))) return;
    if (d->notify) d->notify(msg_type, ext1, ext2, d->user);
}

void camera_data_cb(int32_t msg_type, const camera_memory_t *data, unsigned int index,
        camera_frame_metadata_t *metadata, void *user) {
    wrapper_camera_device_t *d = (wrapper_camera_device_t *)user;
    if (d->hdr && d->hdr->onData(msg_type, data, index)) return;
    // A delayed native NV21 callback must never escape as a JPEG to Camera1.
    if (d->hdr_selected && msg_type == CAMERA_MSG_COMPRESSED_IMAGE) return;
    if (d->data) d->data(msg_type, data, index, metadata, d->user);
}

void camera_data_cb_timestamp(nsecs_t timestamp, int32_t msg_type,
        const camera_memory_t *data, unsigned index, void *user) {
    wrapper_camera_device_t *d = (wrapper_camera_device_t *)user;
    if (d->timestamp) d->timestamp(timestamp, msg_type, data, index, d->user);
}

template <unsigned int Id>
camera_memory_t* camera_get_memory(int fd, size_t buf_size,
        uint_t num_bufs, void *user) {
    (void)user;
    wrapper_camera_device_t *d = gOpenDevices[Id];
    return d && d->memory ? d->memory(fd, buf_size, num_bufs, d->user) : NULL;
}

static void camera_set_callbacks(struct camera_device *device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    wrapper_camera_device_t *d = (wrapper_camera_device_t *)device;
    d->notify = notify_cb;
    d->data = data_cb;
    d->timestamp = data_cb_timestamp;
    d->memory = get_memory;
    d->user = user;

    VENDOR_CALL(device, set_callbacks, camera_notify_cb, camera_data_cb,
            camera_data_cb_timestamp, d->id == 0 ? camera_get_memory<0> : camera_get_memory<1>, d);
}

static void camera_enable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto* wrapper = (wrapper_camera_device_t*)device;
    if (wrapper->hdr) wrapper->hdr->enableMessages(msg_type);
    else VENDOR_CALL(device, enable_msg_type, msg_type);
}

static void camera_disable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto* wrapper = (wrapper_camera_device_t*)device;
    if (wrapper->hdr) wrapper->hdr->disableMessages(msg_type);
    else VENDOR_CALL(device, disable_msg_type, msg_type);
}

static int camera_msg_type_enabled(struct camera_device *device,
        int32_t msg_type)
{
    if (!device)
        return 0;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto* wrapper = (wrapper_camera_device_t*)device;
    return wrapper->hdr ? wrapper->hdr->messagesEnabled(msg_type) :
                          VENDOR_CALL(device, msg_type_enabled, msg_type);
}

static int camera_start_preview(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    if (wrapper->hdr && wrapper->hdr->active()) return -EBUSY;
    return VENDOR_CALL(device, start_preview);
}

static void camera_stop_preview(struct camera_device *device)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    if (wrapper->hdr) wrapper->hdr->cancel();
    VENDOR_CALL(device, stop_preview);
}

static int camera_preview_enabled(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, preview_enabled);
}

static int camera_store_meta_data_in_buffers(struct camera_device *device,
        int enable)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, store_meta_data_in_buffers, enable);
}

static int camera_start_recording(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    if (wrapper->hdr && wrapper->hdr->active()) return -EBUSY;
    if (wrapper->hdr_selected) {
        char* raw = VENDOR_CALL(device, get_parameters);
        if (!raw) return -EINVAL;
        android::CameraParameters params{android::String8(raw)};
        VENDOR_CALL(device, put_parameters, raw);
        params.set(android::CameraParameters::KEY_SCENE_MODE, "auto");
        params.set(android::CameraParameters::KEY_PICTURE_FORMAT, "jpeg");
        params.set(android::CameraParameters::KEY_RECORDING_HINT, "true");
        int result = camera_set_parameters(device, params.flatten().string());
        if (result) return result;
    }
    return VENDOR_CALL(device, start_recording);
}

static void camera_stop_recording(struct camera_device *device)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, stop_recording);
}

static int camera_recording_enabled(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, recording_enabled);
}

static void camera_release_recording_frame(struct camera_device *device,
        const void *opaque)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, release_recording_frame, opaque);
}

static int camera_auto_focus(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, auto_focus);
}

static int camera_cancel_auto_focus(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, cancel_auto_focus);
}

static int camera_take_picture(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    if (wrapper->hdr_selected) {
        if (VENDOR_CALL(device, recording_enabled)) return -EBUSY;
        return wrapper->hdr->takePicture();
    }
    return VENDOR_CALL(device, take_picture);
}

static int camera_cancel_picture(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    if (wrapper->hdr) wrapper->hdr->cancel();
    return VENDOR_CALL(device, cancel_picture);
}

static int camera_set_parameters(struct camera_device *device, const char *params)
{
    if (!device || !params) return -EINVAL;
    auto* wrapper = (wrapper_camera_device_t*)device;
    if (wrapper->hdr && wrapper->hdr->active()) return -EBUSY;
    android::CameraParameters requested{android::String8(params)};
    const char* scene = requested.get(android::CameraParameters::KEY_SCENE_MODE);
    const char* hint = requested.get(android::CameraParameters::KEY_RECORDING_HINT);
    bool hdr = scene && !strcmp(scene, "hdr") && !(hint && !strcmp(hint, "true"));
    if (hdr && (!wrapper->hdr || !wrapper->hdr->available())) return -EINVAL;
    std::unique_ptr<char, decltype(&free)> fixed(camera_fixup_setparams(device, params), free);
    if (!fixed) return -ENOMEM;
    bool oldHdr = wrapper->hdr_selected;
    bool changed = hdr != oldHdr;
    if (changed && VENDOR_CALL(device, recording_enabled)) return -EBUSY;
    bool preview = changed && VENDOR_CALL(device, preview_enabled);
    std::string previous;
    if (changed) {
        char* raw = VENDOR_CALL(device, get_parameters);
        if (!raw) return -EINVAL;
        previous = raw;
        VENDOR_CALL(device, put_parameters, raw);
        if (preview) VENDOR_CALL(device, stop_preview);
    }
    int result = changed ? VENDOR_CALL(device, send_command, 1014, hdr ? 1 : 0, 0) : 0;
    if (!result) result = VENDOR_CALL(device, set_parameters, fixed.get());
    if (!result && preview) result = VENDOR_CALL(device, start_preview);
    if (result && changed) {
        // Restore both the native mode and stream configuration, including a
        // failed preview restart. Commit the public HDR state only on success.
        VENDOR_CALL(device, send_command, 1014, oldHdr ? 1 : 0, 0);
        VENDOR_CALL(device, set_parameters, previous.c_str());
        if (preview) VENDOR_CALL(device, start_preview);
    }
    if (!result) wrapper->hdr_selected = hdr;
    return result;
}

static char *camera_get_parameters(struct camera_device *device)
{
    if (!device)
        return NULL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    auto *wrapper = (wrapper_camera_device_t *)device;
    std::string saved;
    if (wrapper->hdr && wrapper->hdr->parameters(&saved))
        return camera_fixup_getparams(wrapper, saved.c_str());
    char *params = VENDOR_CALL(device, get_parameters);
    if (!params) return NULL;

    char *tmp = camera_fixup_getparams((wrapper_camera_device_t *)device, params);
    VENDOR_CALL(device, put_parameters, params);
    params = tmp;
    return params;
}

static void camera_put_parameters(struct camera_device *, char *params)
{
    if (params)
        free(params);
}

static int camera_send_command(struct camera_device *device,
        int32_t cmd, int32_t arg1, int32_t arg2)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, send_command, cmd, arg1, arg2);
}

static void camera_release(struct camera_device *device)
{
    wrapper_camera_device_t* wrapper_dev = NULL;

    if (!device)
        return;

    wrapper_dev = (wrapper_camera_device_t*) device;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (wrapper_dev->camera_released) return;
    if (wrapper_dev->hdr) wrapper_dev->hdr->cancel();
    VENDOR_CALL(device, release);

    wrapper_dev->camera_released = true;
}

static int camera_dump(struct camera_device *device, int fd)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, dump, fd);
}

extern "C" void heaptracker_free_leaked_memory(void);

static int camera_device_close(hw_device_t *device)
{
    int ret = 0;
    wrapper_camera_device_t *wrapper_dev = NULL;

    ALOGV("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraWrapperLock);

    if (!device) {
        ret = -EINVAL;
        goto done;
    }

    wrapper_dev = (wrapper_camera_device_t*) device;

    if (wrapper_dev->hdr) wrapper_dev->hdr->cancel();
    if (!wrapper_dev->camera_released) {
        ALOGI("%s: releasing camera device with id %d", __FUNCTION__,
                wrapper_dev->id);

        VENDOR_CALL(wrapper_dev, release);

        wrapper_dev->camera_released = true;
    }

    wrapper_dev->vendor->common.close((hw_device_t*)wrapper_dev->vendor);
    gOpenDevices[wrapper_dev->id] = NULL;
    delete wrapper_dev->hdr;
    if (wrapper_dev->base.ops)
        free(wrapper_dev->base.ops);
    delete wrapper_dev;
done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */

static void hdr_complete(void *user, const std::vector<uint8_t>& jpeg) {
    auto *d = static_cast<wrapper_camera_device_t *>(user);
    camera_memory_t *memory = d->memory ? d->memory(-1, jpeg.size(), 1, d->user) : NULL;
    if (!memory || !memory->data || memory->size < jpeg.size()) {
        if (memory) memory->release(memory);
        if (d->notify) d->notify(CAMERA_MSG_ERROR, CAMERA_ERROR_UNKNOWN, 0, d->user);
        return;
    }
    memcpy(memory->data, jpeg.data(), jpeg.size());
    if (d->data) d->data(CAMERA_MSG_COMPRESSED_IMAGE, memory, 0, NULL, d->user);
    memory->release(memory);
}
static void hdr_error(void *user) {
    auto *d = static_cast<wrapper_camera_device_t *>(user);
    if (d->notify) d->notify(CAMERA_MSG_ERROR, CAMERA_ERROR_UNKNOWN, 0, d->user);
}

static int camera_device_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    int rv = 0;
    int num_cameras = 0;
    int cameraid;
    wrapper_camera_device_t *camera_device = NULL;
    camera_device_ops_t *camera_ops = NULL;

    android::Mutex::Autolock lock(gCameraWrapperLock);

    ALOGV("%s", __FUNCTION__);

    if (!name || !device || (strcmp(name, "0") && strcmp(name, "1"))) return -EINVAL;
    *device = NULL;
    if (name != NULL) {
        if (check_vendor_module())
            return -EINVAL;

        cameraid = atoi(name);
        num_cameras = gVendorModule->get_number_of_cameras();

        if (cameraid < 0 || cameraid >= num_cameras || cameraid >= 2) {
            ALOGE("camera service provided cameraid out of bounds, "
                    "cameraid = %d, num supported = %d",
                    cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }
        if (gOpenDevices[cameraid]) {
            rv = -EBUSY;
            goto fail;
        }

        camera_device = new (std::nothrow) wrapper_camera_device_t{};
        if (!camera_device) {
            ALOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }
        camera_device->camera_released = false;
        camera_device->id = cameraid;

        rv = gVendorModule->common.methods->open(
                (const hw_module_t*)gVendorModule, name,
                (hw_device_t**)&(camera_device->vendor));
        if (rv) {
            ALOGE("vendor camera open fail");
            goto fail;
        }
        if (property_get_bool("persist.vendor.camera.samsung", true)) {
            // Samsung Camera performs this handshake before configuring its
            // parameters. It enables the vendor's Samsung processing path.
            rv = camera_device->vendor->ops->send_command(camera_device->vendor, 1508, 0, 0);
            if (rv) goto fail;
        }
        ALOGV("%s: got vendor camera device 0x%08X",
                __FUNCTION__, (uintptr_t)(camera_device->vendor));

        camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
        if (!camera_ops) {
            ALOGE("camera_ops allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        memset(camera_ops, 0, sizeof(*camera_ops));

        camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = CAMERA_DEVICE_API_VERSION_1_0;
        camera_device->base.common.module = (hw_module_t *)(module);
        camera_device->base.common.close = camera_device_close;
        camera_device->base.ops = camera_ops;

        camera_ops->set_preview_window = camera_set_preview_window;
        camera_ops->set_callbacks = camera_set_callbacks;
        camera_ops->enable_msg_type = camera_enable_msg_type;
        camera_ops->disable_msg_type = camera_disable_msg_type;
        camera_ops->msg_type_enabled = camera_msg_type_enabled;
        camera_ops->start_preview = camera_start_preview;
        camera_ops->stop_preview = camera_stop_preview;
        camera_ops->preview_enabled = camera_preview_enabled;
        camera_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        camera_ops->start_recording = camera_start_recording;
        camera_ops->stop_recording = camera_stop_recording;
        camera_ops->recording_enabled = camera_recording_enabled;
        camera_ops->release_recording_frame = camera_release_recording_frame;
        camera_ops->auto_focus = camera_auto_focus;
        camera_ops->cancel_auto_focus = camera_cancel_auto_focus;
        camera_ops->take_picture = camera_take_picture;
        camera_ops->cancel_picture = camera_cancel_picture;
        camera_ops->set_parameters = camera_set_parameters;
        camera_ops->get_parameters = camera_get_parameters;
        camera_ops->put_parameters = camera_put_parameters;
        camera_ops->send_command = camera_send_command;
        camera_ops->release = camera_release;
        camera_ops->dump = camera_dump;

        if (cameraid == 0 && property_get_bool("persist.vendor.camera.hdr", true) &&
            property_get_bool("persist.vendor.camera.samsung", true))
            camera_device->hdr = new StockHdr(camera_device->vendor, hdr_complete, hdr_error, camera_device);
        *device = &camera_device->base.common;
        gOpenDevices[cameraid] = camera_device;
    }

    return rv;

fail:
    if (camera_device) {
        if (camera_device->vendor) {
            camera_device->vendor->ops->release(camera_device->vendor);
            camera_device->vendor->common.close((hw_device_t*)camera_device->vendor);
        }
        delete camera_device;
        camera_device = NULL;
    }
    if (camera_ops) {
        free(camera_ops);
        camera_ops = NULL;
    }
    *device = NULL;
    return rv;
}

static int camera_get_number_of_cameras(void)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;
    return gVendorModule->get_number_of_cameras();
}

static int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    if (!info || camera_id < 0 || camera_id >= camera_get_number_of_cameras())
        return -EINVAL;
    // Samsung's CSG3 getCapabilities constructs Android 6 CameraMetadata on
    // its stack. This legacy metadata path crashes when called by Android 12.
    // Both devices implement device API 1.0. Use their A300FU stock facing
    // and orientation without invoking
    // that incompatible metadata path; leave the OEM image pipeline intact.
    memset(info, 0, sizeof(*info));
    info->facing = camera_id == 0 ? CAMERA_FACING_BACK : CAMERA_FACING_FRONT;
    info->orientation = camera_id == 0 ? 90 : 270;
    info->device_version = CAMERA_DEVICE_API_VERSION_1_0;
    info->resource_cost = 100;
    return 0;
}

// Samsung's module 2.4 flash manager refuses the first camera open until the
// framework's callbacks are registered. Preserve the stock module protocol
// while exposing Camera1 devices through the wrapper.
static int camera_module_set_callbacks(const camera_module_callbacks_t *callbacks) {
    if (check_vendor_module()) return -ENODEV;
    return gVendorModule->set_callbacks ? gVendorModule->set_callbacks(callbacks) : -ENOSYS;
}
static int camera_module_set_torch(const char *id, bool enabled) {
    if (check_vendor_module()) return -ENODEV;
    return gVendorModule->set_torch_mode ? gVendorModule->set_torch_mode(id, enabled) : -ENOSYS;
}
