/*
 * Copyright (C) 2019-2020 Vladimir Bely <vlwwwwww@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "audio_hw_amplifier_tfa"
#include "tfa.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <log/log.h>
#include <pthread.h>
#include <sound/asound.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <tinyalsa/asoundlib.h>
#include <unistd.h>

#include "tfa_dsp.h"

#define TFA_DEVICE "/dev/i2c-1"
#define TFA_ADDRESS 0x34
#define TFA_CONFIG "/vendor/etc/Tfa9895.cnt"
#define CLOCK_ROUTE "QUAT_MI2S_RX Audio Mixer MultiMedia1"

struct tfa_device {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t monitor;
    bool monitor_started;
    bool closing;
    bool requested_on;
    unsigned int requested_profile;
    unsigned int requested_step;
    struct tfa_container container;
    uint8_t *image;
    struct tfa_dsp dsp;
    int fd;
    struct mixer *mixer;
    struct pcm *pcm;
    struct mixer_ctl *clock_ctl;
    bool route_changed;
    pthread_t clock_thread;
    bool clock_thread_started;
    atomic_bool feed_clock;
    atomic_int clock_error;
};

static int transfer(tfa_device_t *d, struct i2c_msg *messages, unsigned int count)
{
    struct i2c_rdwr_ioctl_data request = {.msgs = messages, .nmsgs = count};
    int rc;
    do {
        rc = ioctl(d->fd, I2C_RDWR, &request);
    } while (rc < 0 && errno == EINTR);
    return rc < 0 ? -errno : (rc == (int)count ? 0 : -EIO);
}

static int bus_read(void *context, uint8_t reg, void *data, size_t size)
{
    if (!data || !size || size > 252) return -EINVAL;
    struct i2c_msg messages[] = {
        {.addr = TFA_ADDRESS, .len = 1, .buf = &reg},
        {.addr = TFA_ADDRESS, .flags = I2C_M_RD, .len = size, .buf = data},
    };
    return transfer(context, messages, 2);
}

static int bus_write(void *context, uint8_t reg, const void *data, size_t size)
{
    if (size > 252 || (!data && size)) return -EINVAL;
    uint8_t bytes[253];
    bytes[0] = reg;
    if (size) memcpy(bytes + 1, data, size);
    struct i2c_msg message = {.addr = TFA_ADDRESS, .len = size + 1, .buf = bytes};
    return transfer(context, &message, 1);
}

static void bus_sleep(void *context, unsigned int usec)
{
    (void)context;
    usleep(usec);
}

/* Keep I2S alive for the entire DSP transaction, not just one silent buffer. */
static void *feed_clock(void *context)
{
    tfa_device_t *d = context;
    int fd = pcm_get_poll_fd(d->pcm);
    const int16_t silence[256 * 2] = {0};
    while (atomic_load(&d->feed_clock)) {
        struct snd_xferi x = {.buf = (void *)silence, .frames = 256};
        if (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
            int error = errno;
            if (error == EINTR) continue;
            if (error == EPIPE) {
                if (!ioctl(fd, SNDRV_PCM_IOCTL_PREPARE)) continue;
                error = errno;
            }
            if (error != EAGAIN) {
                atomic_store(&d->clock_error, -error);
                break;
            }
        }
        usleep(1000);
    }
    return NULL;
}

static int clock_release(tfa_device_t *d)
{
    if (d->clock_thread_started) {
        atomic_store(&d->feed_clock, false);
        pthread_join(d->clock_thread, NULL);
        d->clock_thread_started = false;
    }
    if (d->pcm) {
        pcm_close(d->pcm);
        d->pcm = NULL;
    }
    int rc = 0;
    if (d->route_changed) {
        rc = mixer_ctl_set_value(d->clock_ctl, 0, 0);
        d->route_changed = false;
    }
    return rc < 0 ? rc : 0;
}

static bool clocks_present(tfa_device_t *d)
{
    uint16_t status;
    return !tfa_dsp_read_register(&d->dsp, 0, &status) && (status & 0x40);
}

static int clock_acquire(tfa_device_t *d)
{
    /* Playback/voice can already own the clock. Do not close its PCM or route. */
    if (!d->dsp.powered) {
        int rc = tfa_dsp_prepare_clock(&d->dsp);
        if (rc) return rc;
        usleep(2000);
    }
    if (clocks_present(d)) return 0;
    d->clock_ctl = mixer_get_ctl_by_name(d->mixer, CLOCK_ROUTE);
    if (!d->clock_ctl) return -ENODEV;
    int value = mixer_ctl_get_value(d->clock_ctl, 0);
    if (value < 0) return value;
    if (!value) {
        int rc = mixer_ctl_set_value(d->clock_ctl, 0, 1);
        if (rc < 0) return rc;
        d->route_changed = true;
    }
    usleep(10000);
    if (clocks_present(d)) return 0;
    struct pcm_config config = {
        .channels = 2,
        .rate = 48000,
        .period_size = 256,
        .period_count = 4,
        .format = PCM_FORMAT_S16_LE,
        .start_threshold = 256,
        .stop_threshold = UINT_MAX,
        .avail_min = 1,
    };
    d->pcm = pcm_open(0, 0, PCM_OUT | PCM_MONOTONIC, &config);
    int rc = -EIO;
    if (!d->pcm || !pcm_is_ready(d->pcm)) {
        ALOGE("Clock PCM unavailable: %s", d->pcm ? pcm_get_error(d->pcm) : "allocation failed");
        goto fail;
    }
    int fd = pcm_get_poll_fd(d->pcm);
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) {
        rc = -errno;
        goto fail;
    }
    atomic_store(&d->clock_error, 0);
    atomic_store(&d->feed_clock, true);
    rc = pthread_create(&d->clock_thread, NULL, feed_clock, d);
    if (rc) {
        rc = -rc;
        goto fail;
    }
    d->clock_thread_started = true;
    for (unsigned int i = 0; i < 100; ++i) {
        if (clocks_present(d)) return 0;
        rc = atomic_load(&d->clock_error);
        if (rc) goto fail;
        usleep(1000);
    }
    rc = -ETIMEDOUT;
fail:
    (void)clock_release(d);
    return rc;
}

static int apply_locked(tfa_device_t *d)
{
    if (!d->requested_on && !d->dsp.powered) return 0;
    int rc = clock_acquire(d);
    if (rc) {
        (void)tfa_dsp_stop(&d->dsp);
        return rc;
    }
    if (d->requested_on)
        rc = tfa_dsp_start(&d->dsp, d->requested_profile, d->requested_step);
    else
        rc = tfa_dsp_stop(&d->dsp);
    int release_rc = clock_release(d);
    if (!rc) rc = release_rc;
    if (rc)
        ALOGE("TFA %s profile=%u step=%u failed: %s (RPC=%u)", d->requested_on ? "start" : "stop",
              d->requested_profile, d->requested_step, strerror(-rc), d->dsp.rpc_status);
    return rc;
}

static void *monitor(void *context)
{
    tfa_device_t *d = context;
    pthread_mutex_lock(&d->lock);
    while (!d->closing) {
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        ++deadline.tv_sec;
        pthread_cond_timedwait(&d->wake, &d->lock, &deadline);
        if (d->closing || !d->requested_on) continue;
        int rc = tfa_dsp_check(&d->dsp, d->requested_profile, d->requested_step);
        if (rc > 0)
            ALOGW("Recovered TFA profile=%u step=%u", d->requested_profile, d->requested_step);
    }
    pthread_mutex_unlock(&d->lock);
    return NULL;
}

int tfa_apply_volume(tfa_device_t *d, bool enable, tfa_mode_t profile, unsigned int vstep,
                     bool software_volume)
{
    if (!d || (unsigned int)profile >= TFA_PROFILE_COUNT ||
        vstep >= d->container.profiles[profile].steps)
        return -EINVAL;
    pthread_mutex_lock(&d->lock);
    d->requested_on = enable;
    d->requested_profile = profile;
    d->requested_step = vstep;
    bool changed = d->dsp.profile != (int)profile || d->dsp.vstep != vstep ||
                   d->dsp.applied_software_volume != software_volume;
    d->dsp.software_volume = software_volume;
    int rc = apply_locked(d);
    if (!rc && enable && changed)
        ALOGI("Applied TFA profile=%u step=%u volume=%s", profile, vstep,
              software_volume ? "Android" : "DSP");
    pthread_mutex_unlock(&d->lock);
    return rc;
}

int tfa_apply(tfa_device_t *d, bool enable, tfa_mode_t profile, unsigned int vstep)
{
    return tfa_apply_volume(d, enable, profile, vstep, false);
}

const char *tfa_profile_name(tfa_device_t *d, unsigned int profile)
{
    return d && profile < TFA_PROFILE_COUNT ? d->container.profiles[profile].name : NULL;
}

int tfa_find_profile(tfa_device_t *d, const char *name)
{
    return d ? tfa_container_profile(&d->container, name) : -EINVAL;
}

unsigned int tfa_profile_steps(tfa_device_t *d, unsigned int profile)
{
    return d && profile < TFA_PROFILE_COUNT ? d->container.profiles[profile].steps : 0;
}

tfa_device_t *tfa_device_open(void)
{
    tfa_device_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->fd = -1;
    int file = open(TFA_CONFIG, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (file < 0) goto fail;
    if (fstat(file, &st) || st.st_size < 52 || st.st_size > 1024 * 1024) {
        close(file);
        goto fail;
    }
    d->image = malloc(st.st_size);
    if (!d->image) {
        close(file);
        goto fail;
    }
    size_t offset = 0;
    while (offset < (size_t)st.st_size) {
        ssize_t n = read(file, d->image + offset, st.st_size - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        offset += n;
    }
    close(file);
    if (offset != (size_t)st.st_size || tfa_container_parse(&d->container, d->image, offset)) {
        ALOGE("Invalid TFA container %s", TFA_CONFIG);
        goto fail;
    }
    d->fd = open(TFA_DEVICE, O_RDWR | O_CLOEXEC);
    if (d->fd < 0 || !(d->mixer = mixer_open(0))) goto fail;
    d->dsp.io = (struct tfa_io){d, bus_read, bus_write, bus_sleep};
    d->dsp.container = &d->container;
    d->dsp.profile = -1;
    uint16_t revision, system;
    if (tfa_dsp_read_register(&d->dsp, 3, &revision) || (revision & 0xff) != 0x12 ||
        tfa_dsp_read_register(&d->dsp, 9, &system))
        goto fail;
    d->dsp.powered = !(system & 1);
    if (pthread_mutex_init(&d->lock, NULL)) goto fail;
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    int rc = pthread_cond_init(&d->wake, &attr);
    pthread_condattr_destroy(&attr);
    if (rc) {
        pthread_mutex_destroy(&d->lock);
        goto fail;
    }
    atomic_init(&d->feed_clock, false);
    atomic_init(&d->clock_error, 0);
    if (pthread_create(&d->monitor, NULL, monitor, d)) {
        pthread_cond_destroy(&d->wake);
        pthread_mutex_destroy(&d->lock);
        goto fail;
    }
    d->monitor_started = true;
    ALOGI("TFA9895 revision=%04x: loaded all %u stock profiles", revision, TFA_PROFILE_COUNT);
    return d;
fail:
    if (d->mixer) mixer_close(d->mixer);
    if (d->fd >= 0) close(d->fd);
    free(d->image);
    free(d);
    return NULL;
}

void tfa_device_close(tfa_device_t *d)
{
    if (!d) return;
    pthread_mutex_lock(&d->lock);
    d->closing = true;
    d->requested_on = false;
    pthread_cond_signal(&d->wake);
    pthread_mutex_unlock(&d->lock);
    if (d->monitor_started) pthread_join(d->monitor, NULL);
    pthread_mutex_lock(&d->lock);
    (void)apply_locked(d);
    pthread_mutex_unlock(&d->lock);
    (void)clock_release(d);
    mixer_close(d->mixer);
    close(d->fd);
    pthread_cond_destroy(&d->wake);
    pthread_mutex_destroy(&d->lock);
    free(d->image);
    free(d);
}
