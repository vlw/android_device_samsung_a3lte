/*
 * Copyright (C) 2019 Vladimir Bely <vlwwwwww@gmail.com>
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

#ifndef TFA_H
#define TFA_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef enum {
    Audio_Mode_Music_Normal=0,
    Audio_Mode_Voice_NB,
    Audio_Mode_Max,
} tfa_mode_t;

typedef struct {
    bool flag_power;
    bool flag_pll_lock;
    bool flag_otp_ok;
    bool flag_ovp_ok;
    bool flag_uvp_ok;
    bool flag_ocp_alarm;
    bool flag_clocks_stable;
    bool flag_clip;
    bool flag_mtp_busy;
    bool flag_power_ok_bst;
    bool flag_cf_spk_error;
    bool flag_cold_started;
    bool flag_sws;
    bool flag_watchdog_reset;
    bool flag_enable_amp;
    bool flag_enable_ref;
    unsigned char flag[2];
} tfa_status_t;

typedef struct {
    bool flag_powerdown;
    bool flag_reset;
    bool flag_enable_cf;
    bool flag_enable_amp;
    bool flag_enable_boost;
    bool flag_cf_configured;
    bool flag_amp_configured;
    bool flag_dcdcoff;
    unsigned char flag[2];
} tfa_control_t;

typedef struct {
    pthread_mutex_t tfa_lock;
    tfa_mode_t mode;
    struct pcm *pcm;
    struct mixer *mixer;
    int fd;
    tfa_status_t status;
    tfa_control_t control;

    bool clock_enabled;
    bool enabled;
} tfa_device_t;

/*
 * Public API
 */
int tfa_power(tfa_device_t *tfa_dev, bool on);
tfa_device_t * tfa_device_open();
void tfa_device_close(tfa_device_t *tfa_dev);

#endif // TFA_H
