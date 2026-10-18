/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A3_TFA_DSP_H
#define A3_TFA_DSP_H

#include <stdbool.h>

#include "tfa_container.h"

struct tfa_io {
    void *context;
    int (*read)(void *context, uint8_t reg, void *data, size_t size);
    int (*write)(void *context, uint8_t reg, const void *data, size_t size);
    void (*sleep_us)(void *context, unsigned int usec);
};

struct tfa_dsp {
    struct tfa_io io;
    const struct tfa_container *container;
    bool configured;
    bool powered;
    bool drc_supported;
    bool software_volume;
    bool applied_software_volume;
    int profile;
    unsigned int vstep;
    uint32_t rpc_status;
};

int tfa_dsp_read_register(struct tfa_dsp *dsp, uint8_t reg, uint16_t *value);
int tfa_dsp_read_memory(struct tfa_dsp *dsp, uint16_t address, int32_t *value);
int tfa_dsp_prepare_clock(struct tfa_dsp *dsp);
int tfa_dsp_load_profile(struct tfa_dsp *dsp, unsigned int profile, unsigned int step);
int tfa_dsp_start(struct tfa_dsp *dsp, unsigned int profile, unsigned int step);
int tfa_dsp_stop(struct tfa_dsp *dsp);
/* For a requested active route: 1 recovered, 0 healthy, -EAGAIN waiting.
 * Never creates I2S clocks; probes a powered-down chip with its amplifier muted. */
int tfa_dsp_check(struct tfa_dsp *dsp, unsigned int profile, unsigned int step);
int tfa_dsp_calibration(struct tfa_dsp *dsp, float *impedance);

#endif
