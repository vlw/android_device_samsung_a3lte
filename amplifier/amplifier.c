/*
 * Copyright (C) 2019-2020 Vladimir Bely <vlwwwwww@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "audio_hw_amplifier"
#include <cutils/str_parms.h>
#include <errno.h>
#include <hardware/audio_amplifier.h>
#include <limits.h>
#include <log/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <system/audio.h>
/* The private platform header uses this audio_hw.h typedef in its declarations. */
typedef int snd_device_t;
#include <msm8916/platform.h>

#include "tfa.h"
#include "tfa_volume.h"

struct android_volume {
    unsigned int index, maximum;
};

typedef struct {
    amplifier_device_t interface;
    pthread_mutex_t lock;
    tfa_device_t *tfa;
    audio_mode_t mode;
    bool active[SND_DEVICE_OUT_END];
    bool extra_volume;
    int override_profile;
    tfa_mode_t profile;
    unsigned int step;
    bool auto_volume;
    struct android_volume music_volume, voice_volume;
} amp_device_t;

static pthread_mutex_t module_lock = PTHREAD_MUTEX_INITIALIZER;
static amp_device_t *instance;

/* These are internal snd_device_t values, NOT AUDIO_DEVICE_OUT_* bit masks. */
static int classify(uint32_t device)
{
    switch (device) {
        case SND_DEVICE_OUT_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_EXTERNAL_1:
        case SND_DEVICE_OUT_SPEAKER_EXTERNAL_2:
        case SND_DEVICE_OUT_SPEAKER_REVERSE:
        case SND_DEVICE_OUT_SPEAKER_WSA:
        case SND_DEVICE_OUT_SPEAKER_VBAT:
        case SND_DEVICE_OUT_SPEAKER_PROTECTED:
        case SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT:
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
        case SND_DEVICE_OUT_SPEAKER_AND_LINE:
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1:
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2:
        case SND_DEVICE_OUT_SPEAKER_AND_HDMI:
        case SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET:
        case SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET:
            return Audio_Mode_Music_Normal;
        case SND_DEVICE_OUT_VOICE_SPEAKER:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2:
        case SND_DEVICE_OUT_VOICE_SPEAKER_WSA:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA:
        case SND_DEVICE_OUT_VOICE_SPEAKER_VBAT:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT:
        case SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED:
        case SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT:
            return Audio_Mode_Voice_NB;
#ifdef RECORD_PLAY_CONCURRENCY
        case SND_DEVICE_OUT_VOIP_SPEAKER:
            return Audio_Mode_VoIP;
#endif
        default:
            return -1;
    }
}

static tfa_mode_t select_profile(amp_device_t *d, bool *on)
{
    bool voice = false, voip = false;
    *on = false;
    for (unsigned int i = 0; i < SND_DEVICE_OUT_END; ++i) {
        if (!d->active[i]) continue;
        *on = true;
        voice |= classify(i) == Audio_Mode_Voice_NB;
        voip |= classify(i) == Audio_Mode_VoIP;
    }
    if (d->override_profile >= 0) return d->override_profile;
    if (voice || d->mode == AUDIO_MODE_IN_CALL)
        return d->extra_volume ? Audio_Mode_Voice_NB_Extra : Audio_Mode_Voice_NB;
    if (voip || d->mode == AUDIO_MODE_IN_COMMUNICATION) return Audio_Mode_VoIP;
    return Audio_Mode_Music_Normal;
}

static int sync_locked(amp_device_t *d)
{
    bool on;
    tfa_mode_t profile = select_profile(d, &on);
    if (profile != d->profile) {
        d->profile = profile;
        /* Samsung resets the DSP volume step to maximum on a use-case change. */
        d->step = 0;
    }
    const struct android_volume *volume =
        profile == Audio_Mode_Music_Normal ? &d->music_volume : &d->voice_volume;
    bool software = d->auto_volume && volume->maximum;
    if (software)
        d->step =
            tfa_volume_step(volume->index, volume->maximum, tfa_profile_steps(d->tfa, profile));
    return tfa_apply_volume(d->tfa, on, d->profile, d->step, software);
}

static int parse_volume(const char *text, struct android_volume *volume)
{
    char *end;
    errno = 0;
    long index = strtol(text, &end, 10);
    if (errno || end == text || *end != ',' || index < 0) return -EINVAL;
    const char *range = end + 1;
    long maximum = strtol(range, &end, 10);
    if (errno || end == range || *end || maximum <= 0 || maximum > 1000 || index > maximum)
        return -EINVAL;
    *volume = (struct android_volume){index, maximum};
    return 0;
}

static int enable_output(amplifier_device_t *interface, uint32_t device, bool enable)
{
    amp_device_t *d = (amp_device_t *)interface;
    if (device >= SND_DEVICE_OUT_END || classify(device) < 0) return 0;
    pthread_mutex_lock(&d->lock);
    /* audio.primary already refcounts users of each sound device. */
    d->active[device] = enable;
    int rc = sync_locked(d);
    pthread_mutex_unlock(&d->lock);
    return rc;
}

static int set_mode(amplifier_device_t *interface, audio_mode_t mode)
{
    if (mode < AUDIO_MODE_NORMAL || mode > AUDIO_MODE_IN_COMMUNICATION) return -EINVAL;
    amp_device_t *d = (amp_device_t *)interface;
    pthread_mutex_lock(&d->lock);
    d->mode = mode;
    int rc = sync_locked(d);
    pthread_mutex_unlock(&d->lock);
    return rc;
}

static int set_parameters(amplifier_device_t *interface, struct str_parms *parameters)
{
    amp_device_t *d = (amp_device_t *)interface;
    char name[80];
    int profile = -2, step = -1, extra = -1;
    int auto_volume = -1;
    struct android_volume music = {0}, voice = {0};
    if (str_parms_get_str(parameters, "tfa_android_music", name, sizeof(name)) >= 0 &&
        parse_volume(name, &music))
        return -EINVAL;
    if (str_parms_get_str(parameters, "tfa_android_voice", name, sizeof(name)) >= 0 &&
        parse_volume(name, &voice))
        return -EINVAL;
    if (str_parms_get_str(parameters, "tfa_profile", name, sizeof(name)) >= 0) {
        profile = !strcmp(name, "auto") ? -1 : tfa_find_profile(d->tfa, name);
        if (profile < -1) return -EINVAL;
    }
    if (str_parms_get_str(parameters, "extra_volume", name, sizeof(name)) >= 0) {
        if (!strcmp(name, "true") || !strcmp(name, "on") || !strcmp(name, "1"))
            extra = 1;
        else if (!strcmp(name, "false") || !strcmp(name, "off") || !strcmp(name, "0"))
            extra = 0;
        else
            return -EINVAL;
    }
    if (str_parms_get_str(parameters, "tfa_vstep", name, sizeof(name)) >= 0) {
        if (!strcmp(name, "auto")) {
            auto_volume = 1;
        } else {
            char *end;
            errno = 0;
            long value = strtol(name, &end, 10);
            if (errno || end == name || *end || value < 0 || value > INT_MAX) return -EINVAL;
            step = value;
            auto_volume = 0;
        }
    }
    if (profile == -2 && step == -1 && extra == -1 && auto_volume == -1 && !music.maximum &&
        !voice.maximum)
        return 0;
    pthread_mutex_lock(&d->lock);
    int old_override = d->override_profile;
    bool old_extra = d->extra_volume;
    if (profile != -2) d->override_profile = profile;
    if (extra != -1) d->extra_volume = extra;
    bool on;
    tfa_mode_t selected = select_profile(d, &on);
    if (step < -1 || (step >= 0 && (unsigned int)step >= tfa_profile_steps(d->tfa, selected))) {
        d->override_profile = old_override;
        d->extra_volume = old_extra;
        pthread_mutex_unlock(&d->lock);
        return -EINVAL;
    }
    if (selected != d->profile) {
        d->profile = selected;
        d->step = 0;
    }
    if (step >= 0) d->step = step;
    if (auto_volume >= 0) d->auto_volume = auto_volume;
    if (music.maximum) d->music_volume = music;
    if (voice.maximum) d->voice_volume = voice;
    int rc = sync_locked(d);
    pthread_mutex_unlock(&d->lock);
    return rc;
}

static int close_device(hw_device_t *interface)
{
    amp_device_t *d = (amp_device_t *)interface;
    pthread_mutex_lock(&module_lock);
    tfa_device_close(d->tfa);
    pthread_mutex_destroy(&d->lock);
    instance = NULL;
    free(d);
    pthread_mutex_unlock(&module_lock);
    return 0;
}

static int open_device(const hw_module_t *module, const char *name, hw_device_t **result)
{
    if (!name || !result || strcmp(name, AMPLIFIER_HARDWARE_INTERFACE)) return -EINVAL;
    *result = NULL;
    pthread_mutex_lock(&module_lock);
    if (instance) {
        pthread_mutex_unlock(&module_lock);
        return -EBUSY;
    }
    amp_device_t *d = calloc(1, sizeof(*d));
    int rc = -ENOMEM;
    if (!d) goto fail;
    rc = pthread_mutex_init(&d->lock, NULL);
    if (rc) {
        rc = -rc;
        goto fail;
    }
    d->tfa = tfa_device_open();
    if (!d->tfa) {
        rc = -ENODEV;
        pthread_mutex_destroy(&d->lock);
        goto fail;
    }
    d->override_profile = -1;
    d->auto_volume = true;
    d->mode = AUDIO_MODE_NORMAL;
    d->interface.common = (hw_device_t){
        .tag = HARDWARE_DEVICE_TAG,
        .module = (hw_module_t *)module,
        .version = HARDWARE_DEVICE_API_VERSION(1, 0),
        .close = close_device,
    };
    d->interface.enable_output_devices = enable_output;
    d->interface.set_mode = set_mode;
    d->interface.set_parameters = set_parameters;
    d->interface.out_set_parameters = set_parameters;
    instance = d;
    *result = &d->interface.common;
    pthread_mutex_unlock(&module_lock);
    return 0;
fail:
    free(d);
    pthread_mutex_unlock(&module_lock);
    return rc;
}

static struct hw_module_methods_t methods = {.open = open_device};
amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = AMPLIFIER_MODULE_API_VERSION_0_1,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = AMPLIFIER_HARDWARE_MODULE_ID,
            .name = "Samsung Galaxy A3 TFA9895 amplifier HAL",
            .author = "Vladimir Bely <vlwwwwww@gmail.com>",
            .methods = &methods,
        },
};
