/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A3_TFA_H
#define A3_TFA_H

#include <stdbool.h>

typedef enum {
    Audio_Mode_Music_Normal = 0,
    Audio_Mode_Voice_NB,
    Audio_Mode_Voice_NB_Extra,
    Audio_Mode_Voice_WB,
    Audio_Mode_Voice_WB_Extra,
    Audio_Mode_VT_NB,
    Audio_Mode_VT_WB,
    Audio_Mode_VoIP,
    Audio_Mode_VoLTE,
    Audio_Mode_VoLTE_Extra,
    Audio_Mode_VT_VoLTE,
    Audio_Mode_Max,
} tfa_mode_t;

typedef struct tfa_device tfa_device_t;

tfa_device_t *tfa_device_open(void);
void tfa_device_close(tfa_device_t *device);
int tfa_apply(tfa_device_t *device, bool enable, tfa_mode_t profile, unsigned int vstep);
int tfa_apply_volume(tfa_device_t *device, bool enable, tfa_mode_t profile, unsigned int vstep,
                     bool software_volume);
const char *tfa_profile_name(tfa_device_t *device, unsigned int profile);
int tfa_find_profile(tfa_device_t *device, const char *name);
unsigned int tfa_profile_steps(tfa_device_t *device, unsigned int profile);

#endif
