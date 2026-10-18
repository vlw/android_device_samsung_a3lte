/* SPDX-License-Identifier: Apache-2.0 */
/* TFA9895 protocol reconstructed from SM-A300FU's libtfa9895.so and container. */
#include "tfa_dsp.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#define REG_STATUS 0x00
#define REG_REVISION 0x03
#define REG_I2S 0x04
#define REG_AUDIO 0x06
#define REG_SYSTEM 0x09
#define REG_CF_CONTROL 0x70
#define REG_CF_ADDRESS 0x71
#define REG_CF_MEMORY 0x72
#define REG_CF_STATUS 0x73
#define REG_MTP 0x80
#define STATUS_COLD 0x0800
#define STATUS_SWITCHING 0x1000
#define SYS_POWERDOWN 0x0001
#define SYS_AMP 0x0008
#define SYS_BOOST 0x0010
#define SYS_CONFIGURED 0x0020
#define I2S_OUTPUT 0x0800
#define AUDIO_MUTE 0x0020
#define RPC_CHUNK 252

static void delay(struct tfa_dsp *d, unsigned int usec) { d->io.sleep_us(d->io.context, usec); }

static int write_reg(struct tfa_dsp *d, uint8_t reg, uint16_t value)
{
    const uint8_t bytes[] = {value >> 8, value & 0xff};
    return d->io.write(d->io.context, reg, bytes, sizeof(bytes));
}

int tfa_dsp_read_register(struct tfa_dsp *d, uint8_t reg, uint16_t *value)
{
    uint8_t bytes[2];
    int rc = d->io.read(d->io.context, reg, bytes, sizeof(bytes));
    if (!rc) *value = (uint16_t)bytes[0] << 8 | bytes[1];
    return rc;
}

static int update_reg(struct tfa_dsp *d, uint8_t reg, uint16_t mask, uint16_t value)
{
    uint16_t old;
    int rc = tfa_dsp_read_register(d, reg, &old);
    return rc ? rc : write_reg(d, reg, (old & ~mask) | (value & mask));
}

static int wait_stable(struct tfa_dsp *d)
{
    for (unsigned int i = 0; i < 100; ++i) {
        uint16_t status;
        int rc = tfa_dsp_read_register(d, REG_STATUS, &status);
        if (rc) return rc;
        if ((status & 0x4000) || (status & 0x8040) == 0x8040) return 0;
        delay(d, 1000);
    }
    return -ETIMEDOUT;
}

static int powerdown(struct tfa_dsp *d, bool down)
{
    return update_reg(d, REG_SYSTEM, SYS_POWERDOWN, down ? SYS_POWERDOWN : 0);
}

/* Stock mute modes: off=0, digital=1, amplifier=2. Preserve all other bits. */
static int mute(struct tfa_dsp *d, unsigned int mode)
{
    int rc = update_reg(d, REG_AUDIO, AUDIO_MUTE, mode == 1 ? AUDIO_MUTE : 0);
    if (rc) return rc;
    return update_reg(d, REG_SYSTEM, SYS_AMP | SYS_BOOST, mode == 2 ? 0 : SYS_AMP);
}

static int mute_amplifier(struct tfa_dsp *d)
{
    int rc = mute(d, 2);
    if (rc) return rc;
    for (unsigned int i = 0; i < 150; ++i) {
        uint16_t status;
        rc = tfa_dsp_read_register(d, REG_STATUS, &status);
        if (rc || !(status & STATUS_SWITCHING)) return rc;
        delay(d, 2000);
    }
    return -ETIMEDOUT;
}

static int unmute(struct tfa_dsp *d)
{
    int rc = mute(d, 0);
    return rc ? rc : update_reg(d, REG_SYSTEM, SYS_BOOST, SYS_BOOST);
}

int tfa_dsp_prepare_clock(struct tfa_dsp *d)
{
    /* CLKS is low in powerdown even if another PCM owns a valid external clock. */
    int rc = update_reg(d, REG_AUDIO, AUDIO_MUTE, AUDIO_MUTE);
    if (!rc) rc = update_reg(d, REG_SYSTEM, SYS_POWERDOWN | SYS_AMP | SYS_BOOST, 0);
    return rc;
}

int tfa_dsp_read_memory(struct tfa_dsp *d, uint16_t address, int32_t *value)
{
    uint8_t bytes[3];
    int rc = update_reg(d, REG_CF_CONTROL, 0x000e, 0x0002);
    if (!rc) rc = write_reg(d, REG_CF_ADDRESS, address);
    if (!rc) rc = d->io.read(d->io.context, REG_CF_MEMORY, bytes, sizeof(bytes));
    if (!rc) {
        uint32_t word = (uint32_t)bytes[0] << 16 | (uint32_t)bytes[1] << 8 | bytes[2];
        *value = (word & 0x800000) ? (int32_t)(word | 0xff000000U) : (int32_t)word;
    }
    return rc;
}

static int rpc_finish(struct tfa_dsp *d, unsigned int tries)
{
    int rc = write_reg(d, REG_CF_CONTROL, 0x0112);
    if (rc) return rc;
    bool ack = false;
    for (unsigned int i = 0; i < tries; ++i) {
        uint16_t status;
        rc = tfa_dsp_read_register(d, REG_CF_STATUS, &status);
        if (rc) return rc;
        if (status & 0x0100) {
            ack = true;
            break;
        }
        delay(d, 1000);
    }
    if (!ack) return -ETIMEDOUT;
    const uint8_t select_status[] = {0, 2, 0, 0};
    uint8_t status[3];
    rc = d->io.write(d->io.context, REG_CF_CONTROL, select_status, sizeof(select_status));
    if (!rc) rc = d->io.read(d->io.context, REG_CF_MEMORY, status, sizeof(status));
    if (rc) return rc;
    d->rpc_status = (uint32_t)status[0] << 16 | (uint32_t)status[1] << 8 | status[2];
    return d->rpc_status ? -EREMOTEIO : 0;
}

static int rpc_header(struct tfa_dsp *d, uint8_t module, uint8_t parameter)
{
    const uint8_t header[] = {0, 2, 0, 1, 0, module | 0x80, parameter};
    d->rpc_status = UINT32_MAX;
    return d->io.write(d->io.context, REG_CF_CONTROL, header, sizeof(header));
}

static int rpc_write(struct tfa_dsp *d, uint8_t module, uint8_t parameter, const void *data,
                     size_t size)
{
    if (!data || !size || size % 3) return -EINVAL;
    int rc = rpc_header(d, module, parameter);
    const uint8_t *bytes = data;
    while (!rc && size) {
        size_t chunk = size > RPC_CHUNK ? RPC_CHUNK : size;
        rc = d->io.write(d->io.context, REG_CF_MEMORY, bytes, chunk);
        bytes += chunk;
        size -= chunk;
    }
    return rc ? rc : rpc_finish(d, parameter == 6 && module == 1 ? 2000 : 40);
}

static int rpc_read(struct tfa_dsp *d, uint8_t module, uint8_t parameter, void *data, size_t size)
{
    if (!data || !size || size % 3) return -EINVAL;
    int rc = rpc_header(d, module, parameter);
    if (!rc) rc = rpc_finish(d, 40);
    if (!rc) rc = write_reg(d, REG_CF_ADDRESS, 2);
    uint8_t *bytes = data;
    while (!rc && size) {
        size_t chunk = size > RPC_CHUNK ? RPC_CHUNK : size;
        rc = d->io.read(d->io.context, REG_CF_MEMORY, bytes, chunk);
        bytes += chunk;
        size -= chunk;
    }
    return rc;
}

static int agc_insert(struct tfa_dsp *d)
{
    const uint8_t zero[] = {0, 0, 0};
    return d->drc_supported ? rpc_write(d, 1, 0x10, zero, sizeof(zero)) : 0;
}

int tfa_dsp_load_profile(struct tfa_dsp *d, unsigned int profile, unsigned int step)
{
    if (profile >= TFA_PROFILE_COUNT) return -EINVAL;
    const struct tfa_profile *p = &d->container->profiles[profile];
    if (step >= p->steps) return -EINVAL;
    const uint8_t *vstep = p->vsteps.data + 38 + step * TFA_VSTEP_BYTES;
    /* Android already applies the stream gain in automatic mode. Keep the
     * profile's full-volume hardware attenuation, while selecting the actual
     * volume-dependent stock preset/EQ/DRC. Never attenuate the signal twice. */
    const uint8_t *gain_step = d->software_volume ? p->vsteps.data + 38 : vstep;
    uint32_t bits = tfa_le32(gain_step);
    float attenuation;
    memcpy(&attenuation, &bits, sizeof(attenuation));
    if (!isfinite(attenuation) || attenuation > 0 || attenuation < -127.5f) return -EINVAL;
    int rc = rpc_write(d, 1, 0x0d, vstep + 4, TFA_PRESET_BYTES);
    for (unsigned int i = 0; !rc && i < 10; ++i) {
        const uint8_t *filter = vstep + 4 + TFA_PRESET_BYTES + i * TFA_FILTER_BYTES;
        const uint8_t disabled[18] = {0x80};
        rc = rpc_write(d, 2, i + 1, filter[18] ? filter : disabled, 18);
    }
    if (!rc) rc = update_reg(d, REG_AUDIO, 0xff00, (uint16_t)(-2 * attenuation) << 8);
    if (!rc && d->drc_supported) rc = rpc_write(d, 1, 0x0f, p->drc.data + 36 + 381, 381);
    if (!rc) {
        d->profile = profile;
        d->vstep = step;
        d->applied_software_volume = d->software_volume;
    }
    return rc;
}

static int load_patch(struct tfa_dsp *d)
{
    const struct tfa_span *p = &d->container->patch;
    const uint8_t *patch = p->data + 36;
    uint16_t revision;
    int rc = tfa_dsp_read_register(d, REG_REVISION, &revision);
    if (rc) return rc;
    if (patch[0] != 0xff && patch[0] != (revision & 0xff)) return -ENODEV;
    uint16_t address = (uint16_t)patch[1] << 8 | patch[2];
    if (address != 0xffff) {
        int32_t actual;
        rc = tfa_dsp_read_memory(d, address, &actual);
        uint32_t expected = (uint32_t)patch[3] << 16 | (uint32_t)patch[4] << 8 | patch[5];
        if (rc || ((uint32_t)actual & 0xffffff) != expected) return rc ? rc : -ENODEV;
    }
    size_t pos = 42;
    while (pos < p->size) {
        if (p->size - pos < 2) return -EINVAL;
        size_t size = tfa_le16(p->data + pos);
        pos += 2;
        if (!size || size > 253 || size > p->size - pos) return -EINVAL;
        rc = d->io.write(d->io.context, p->data[pos], p->data + pos + 1, size - 1);
        if (rc) return rc;
        pos += size;
    }
    return 0;
}

static int wait_calibration(struct tfa_dsp *d)
{
    uint16_t mtp;
    int rc = tfa_dsp_read_register(d, REG_MTP, &mtp);
    if (rc) return rc;
    for (unsigned int i = 0; i < 150; ++i) {
        if (mtp & 1) {
            rc = tfa_dsp_read_register(d, REG_MTP, &mtp);
            if (rc || (mtp & 2)) return rc;
        } else {
            int32_t done;
            rc = tfa_dsp_read_memory(d, 231, &done);
            if (rc || done) return rc;
        }
        delay(d, 12500);
    }
    return -ETIMEDOUT;
}

int tfa_dsp_calibration(struct tfa_dsp *d, float *impedance)
{
    uint8_t bytes[3];
    int32_t done;
    int rc = tfa_dsp_read_memory(d, 231, &done);
    if (rc || !done) return rc ? rc : -EAGAIN;
    rc = rpc_read(d, 1, 0x85, bytes, sizeof(bytes));
    if (!rc) {
        uint32_t word = (uint32_t)bytes[0] << 16 | (uint32_t)bytes[1] << 8 | bytes[2];
        *impedance = word / 16384.0f;
    }
    return rc;
}

static int cold_start(struct tfa_dsp *d)
{
    static const struct {
        uint8_t reg;
        uint16_t value;
    } init[] = {
        {9, 2},      {9, 9},      {5, 0x13ab},    {6, 0x001f},
        {8, 0x3c4e}, {9, 0x024d}, {0x41, 0x0308}, {0x49, 0x0e82},
    };
    int rc = 0;
    for (unsigned int i = 0; !rc && i < sizeof(init) / sizeof(init[0]); ++i)
        rc = write_reg(d, init[i].reg, init[i].value);
    for (unsigned int i = 0; !rc && i < d->container->field_count; ++i) {
        uint32_t field = d->container->fields[i];
        uint16_t mask = (uint16_t)((1U << (((field >> 16) & 15) + 1)) - 1);
        unsigned int pos = (field >> 20) & 15;
        if (((field >> 16) & 15) + 1 + pos > 16 || (field & 0xffff) > mask) return -EINVAL;
        rc = update_reg(d, field >> 24, mask << pos, (field & 0xffff) << pos);
    }
    if (!rc) rc = powerdown(d, false);
    if (!rc) rc = wait_stable(d);
    if (!rc) rc = load_patch(d);
    if (rc) return rc;

    uint8_t features[6];
    rc = rpc_read(d, 0, 0x85, features, sizeof(features));
    if (rc && !(rc == -EREMOTEIO && d->rpc_status == 3)) return rc;
    d->drc_supported = !rc && !(features[1] & 2);
    const struct tfa_container *c = d->container;
    rc = mute(d, 1);
    if (!rc) rc = rpc_write(d, 1, 0x0e, c->config.data + 36, c->config.size - 36);
    if (!rc) rc = agc_insert(d);
    if (!rc) rc = rpc_write(d, 1, 0x06, c->speaker.data + 73, c->speaker.size - 73);
    if (!rc) rc = agc_insert(d);
    /* The DSP needs the default preset before SBSL, even for a voice startup. */
    if (!rc) rc = tfa_dsp_load_profile(d, 0, 0);
    if (!rc) rc = update_reg(d, REG_SYSTEM, SYS_CONFIGURED, SYS_CONFIGURED);
    if (!rc) rc = wait_calibration(d);
    if (!rc) {
        float impedance;
        rc = tfa_dsp_calibration(d, &impedance);
        if (!rc && (impedance < 4.0f || impedance > 9.0f)) rc = -ERANGE;
    }
    if (!rc) d->configured = true;
    return rc;
}

int tfa_dsp_stop(struct tfa_dsp *d)
{
    int rc = mute_amplifier(d);
    int down_rc = powerdown(d, true);
    int i2s_rc = update_reg(d, REG_I2S, I2S_OUTPUT, 0);
    d->powered = false;
    if (rc || down_rc || i2s_rc) d->configured = false;
    return rc ? rc : (down_rc ? down_rc : i2s_rc);
}

int tfa_dsp_start(struct tfa_dsp *d, unsigned int profile, unsigned int step)
{
    if (!d || !d->container || profile >= TFA_PROFILE_COUNT ||
        step >= d->container->profiles[profile].steps)
        return -EINVAL;
    uint16_t status, system;
    int rc = tfa_dsp_read_register(d, REG_STATUS, &status);
    if (!rc) rc = tfa_dsp_read_register(d, REG_SYSTEM, &system);
    if (rc) goto fail;
    if ((status & 0x001c) != 0x001c || (status & 0x20)) {
        rc = -EAGAIN;
        goto fail;
    }
    if (!d->configured || (status & (STATUS_COLD | 0x2000)) || !(system & SYS_CONFIGURED)) {
        d->configured = false;
        rc = cold_start(d);
    } else if (!d->powered) {
        rc = update_reg(d, REG_I2S, I2S_OUTPUT, I2S_OUTPUT);
        if (!rc) rc = mute(d, 1);
        if (!rc) rc = powerdown(d, false);
        if (!rc) rc = wait_stable(d);
    }
    if (rc) goto fail;
    if (d->profile != (int)profile || d->vstep != step ||
        d->software_volume != d->applied_software_volume) {
        if (d->profile != (int)profile) {
            rc = mute_amplifier(d);
            if (!rc) rc = powerdown(d, true);
            if (!rc) rc = mute(d, 1);
            if (!rc) rc = powerdown(d, false);
            if (!rc) rc = wait_stable(d);
        } else {
            rc = mute(d, 1);
        }
        if (!rc) rc = tfa_dsp_load_profile(d, profile, step);
    }
    if (!rc) {
        delay(d, 5000);
        rc = unmute(d);
    }
    if (!rc) {
        d->powered = true;
        return 0;
    }
fail:
    (void)tfa_dsp_stop(d);
    d->configured = false;
    return rc;
}

int tfa_dsp_check(struct tfa_dsp *d, unsigned int profile, unsigned int step)
{
    if (!d || !d->container || profile >= TFA_PROFILE_COUNT ||
        step >= d->container->profiles[profile].steps)
        return -EINVAL;
    uint16_t status, system;
    int rc = tfa_dsp_read_register(d, REG_STATUS, &status);
    if (!rc) rc = tfa_dsp_read_register(d, REG_SYSTEM, &system);
    if (rc) return rc;
    if (system & SYS_POWERDOWN) d->powered = false;

    if (!d->powered) {
        /* CLKS cannot indicate an external clock while the chip is powered down.
         * Wake only its muted digital/analog path, then check the real status.
         * This also provides the power cycle used by stock ResolveIncident for
         * a latched overcurrent fault. Do not open a silent PCM from the monitor. */
        rc = tfa_dsp_prepare_clock(d);
        if (rc) goto stop;
        delay(d, 2000);
        rc = tfa_dsp_read_register(d, REG_STATUS, &status);
        if (rc) goto stop;
    }
    if ((status & 0x001c) != 0x001c || (status & 0x20)) {
        rc = -EAGAIN;
        goto stop;
    }
    if (!(status & 0x40)) {
        /* A running route can temporarily lose clocks. Leave it alone, but
         * return an unsuccessful recovery probe to powerdown. */
        if (d->powered) return -EAGAIN;
        rc = -EAGAIN;
        goto stop;
    }
    if ((status & (STATUS_COLD | 0x2000)) || !(system & SYS_CONFIGURED)) d->configured = false;
    if (d->powered && d->configured) return 0;
    rc = tfa_dsp_start(d, profile, step);
    return rc ? rc : 1;
stop : {
    int stop_rc = tfa_dsp_stop(d);
    return stop_rc ? stop_rc : rc;
}
}
