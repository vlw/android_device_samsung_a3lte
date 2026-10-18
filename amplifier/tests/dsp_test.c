/* SPDX-License-Identifier: Apache-2.0 */
#include "tfa_dsp.h"
#include "tfa_volume.h"
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bus {
    uint16_t reg[256];
    uint16_t address;
    uint8_t module, parameter;
    uint8_t payload[2048];
    size_t bytes;
    uint8_t trace[4096];
    size_t traced;
    bool record, ack;
    bool model_clock, external_clock, clear_ocp_on_powerup;
    unsigned int rpc_error;
    int fault_after;
    int operations;
    int32_t memory;
};

static int operation(struct bus *b)
{
    return b->fault_after >= 0 && b->operations++ >= b->fault_after ? -ENXIO : 0;
}

static void trace(struct bus *b, const void *data, size_t size)
{
    if (!b->record) return;
    assert(size <= sizeof(b->trace) - b->traced);
    memcpy(b->trace + b->traced, data, size);
    b->traced += size;
}

static void reset(struct bus *b)
{
    memset(b, 0, sizeof(*b));
    b->reg[0] = 0x885f;
    b->reg[3] = 0x0012;
    b->reg[4] = 0x888b;
    b->reg[6] = 0x001f;
    b->reg[9] = 1;
    b->reg[0x80] = 3;
    b->ack = true;
    b->fault_after = -1;
}

static int read_bus(void *context, uint8_t reg, void *data, size_t size)
{
    struct bus *b = context;
    if (operation(b)) return -ENXIO;
    uint8_t *out = data;
    memset(out, 0, size);
    if (reg == 0x72) {
        uint32_t word = 0;
        if (b->address == 0)
            word = b->rpc_error;
        else if (b->address == 0x21b4)
            word = 0x779a;
        else if (b->address == 231)
            word = 1;
        else if (b->address == 2 && b->module == 1 && b->parameter == 0x85)
            word = 6 * 16384;
        else if (b->address == 123)
            word = b->memory & 0xffffff;
        if (size >= 3) {
            out[0] = word >> 16;
            out[1] = word >> 8;
            out[2] = word;
        }
        return 0;
    }
    assert(size == 2);
    uint16_t value = reg == 0x73 ? (b->ack ? 0x100 : 0) : b->reg[reg];
    if (reg == 0 && b->model_clock) {
        value &= ~0x40;
        if (b->external_clock && !(b->reg[9] & 1)) value |= 0x40;
    }
    out[0] = value >> 8;
    out[1] = value;
    return 0;
}

static int write_bus(void *context, uint8_t reg, const void *data, size_t size)
{
    struct bus *b = context;
    const uint8_t *in = data;
    if (operation(b)) return -ENXIO;
    assert(size <= 252);
    if (reg == 0x70 && size == 7) {
        assert(in[0] == 0 && in[1] == 2 && in[2] == 0 && in[3] == 1);
        b->module = in[5] & 0x7f;
        b->parameter = in[6];
        b->bytes = 0;
        b->address = 1;
    } else if (reg == 0x70 && size == 4) {
        b->address = (uint16_t)in[2] << 8 | in[3];
    } else if (reg == 0x72) {
        assert(size % 3 == 0);
        assert(size <= sizeof(b->payload) - b->bytes);
        memcpy(b->payload + b->bytes, data, size);
        b->bytes += size;
    } else if (reg == 0x71 && size >= 2) {
        b->address = (uint16_t)in[0] << 8 | in[1];
    }
    if (size == 2) {
        uint16_t value = (uint16_t)in[0] << 8 | in[1];
        if (reg == 9 && (b->reg[9] & 1) && !(value & 1) && b->clear_ocp_on_powerup)
            b->reg[0] &= ~0x20;
        b->reg[reg] = value;
        if (reg == 9) {
            if (value == 2) {
                b->reg[9] = 1;
                b->reg[4] = 0x888b;
                b->reg[0] |= 0x800;
            }
            if (value & 0x20) b->reg[0] &= ~0x800;
            if ((value & 8) && !(value & 1))
                b->reg[0] |= 0x1000;
            else
                b->reg[0] &= ~0x1000;
        }
        if (reg == 0x70 && value == 0x112 && b->bytes) {
            uint8_t header[] = {b->module, b->parameter, b->bytes & 255, b->bytes >> 8};
            trace(b, header, sizeof(header));
            trace(b, b->payload, b->bytes);
        }
        if (reg == 6) {
            uint8_t volume[] = {255, value >> 8};
            trace(b, volume, sizeof(volume));
        }
    }
    return 0;
}

static void sleep_bus(void *context, unsigned int usec)
{
    (void)context;
    assert(usec <= 12500);
}

static struct tfa_dsp device(struct bus *b, const struct tfa_container *c)
{
    struct tfa_dsp d = {
        .io = {b, read_bus, write_bus, sleep_bus},
        .container = c,
        .profile = -1,
        .drc_supported = true,
    };
    return d;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    uint8_t image[20000];
    size_t size = fread(image, 1, sizeof(image), f);
    fclose(f);
    struct tfa_container c;
    assert(!tfa_container_parse(&c, image, size));
    assert(tfa_container_profile(&c, "speechVoiceWB") == 3);
    assert(tfa_container_profile(&c, "not-a-profile") == -ENOENT);
    /* Every truncated input must fail without reading beyond its allocation. */
    for (size_t i = 0; i < size; ++i) {
        uint8_t *short_image = malloc(i ? i : 1);
        assert(short_image);
        memcpy(short_image, image, i);
        struct tfa_container invalid;
        assert(tfa_container_parse(&invalid, short_image, i) < 0);
        free(short_image);
    }
    image[size - 1] ^= 1;
    struct tfa_container invalid;
    assert(tfa_container_parse(&invalid, image, size) == -EBADMSG);
    image[size - 1] ^= 1;
    struct bus b;
    reset(&b);
    struct tfa_dsp d = device(&b, &c);
    b.record = true;
    for (unsigned int profile = 0; profile < TFA_PROFILE_COUNT; ++profile) {
        for (unsigned int step = 0; step < c.profiles[profile].steps; ++step) {
            b.traced = 0;
            assert(!tfa_dsp_load_profile(&d, profile, step));
            printf("%u %u ", profile, step);
            for (size_t i = 0; i < b.traced; ++i) printf("%02x", b.trace[i]);
            putchar('\n');
            b.traced = 0;
            d.software_volume = true;
            assert(!tfa_dsp_load_profile(&d, profile, step));
            assert((b.reg[6] >> 8) == (profile == 0 ? 2 : 0));
            printf("auto %u %u ", profile, step);
            for (size_t i = 0; i < b.traced; ++i) printf("%02x", b.trace[i]);
            putchar('\n');
            d.software_volume = false;
        }
    }
    b.record = false;
    for (unsigned int i = 1; i <= 15; ++i) assert(tfa_volume_step(i, 15, 15) == 15 - i);
    for (unsigned int i = 0; i <= 5; ++i) assert(tfa_volume_step(i, 5, 6) == 5 - i);
    for (unsigned int maximum = 1; maximum <= 1000; ++maximum) {
        unsigned int previous = 14;
        for (unsigned int i = 0; i <= maximum; ++i) {
            unsigned int step = tfa_volume_step(i, maximum, 15);
            assert(step <= previous && step < 15);
            previous = step;
        }
        assert(previous == 0);
    }
    assert(tfa_dsp_load_profile(&d, 11, 0) == -EINVAL);
    assert(tfa_dsp_start(&d, 1, 6) == -EINVAL);
    b.ack = false;
    assert(tfa_dsp_load_profile(&d, 0, 0) == -ETIMEDOUT);
    b.ack = true;
    b.rpc_error = 3;
    assert(tfa_dsp_load_profile(&d, 0, 0) == -EREMOTEIO);
    b.rpc_error = 0;
    b.memory = -1;
    int32_t memory;
    assert(!tfa_dsp_read_memory(&d, 123, &memory) && memory == -1);
    reset(&b);
    d = device(&b, &c);
    assert(!tfa_dsp_start(&d, 1, 2));
    assert(d.powered && d.configured && d.profile == 1 && d.vstep == 2);
    assert(!tfa_dsp_start(&d, 0, 0));
    assert(d.powered && d.profile == 0);
    assert(!tfa_dsp_stop(&d));
    assert(!d.powered && (b.reg[9] & 1) && !(b.reg[4] & 0x800));
    assert(!tfa_dsp_start(&d, 7, 0));
    assert(d.powered && d.profile == 7);
    d.software_volume = true;
    assert(!tfa_dsp_start(&d, 7, 5));
    assert(d.applied_software_volume && b.reg[6] >> 8 == 0);
    d.software_volume = false;
    assert(!tfa_dsp_start(&d, 7, 5));
    assert(!d.applied_software_volume && b.reg[6] >> 8 == 30);
    b.reg[0] |= 0x2800; /* Power loss/watchdog: reload patch, model and profile. */
    assert(!tfa_dsp_start(&d, 3, 1));
    assert(d.powered && d.profile == 3);
    reset(&b);
    d = device(&b, &c);
    b.fault_after = 2;
    assert(tfa_dsp_start(&d, 0, 0) == -ENXIO);
    assert(!d.powered && !d.configured);
    reset(&b);
    d = device(&b, &c);
    b.reg[0] |= 0x20;
    assert(tfa_dsp_start(&d, 0, 0) == -EAGAIN);
    assert(!d.powered);
    reset(&b);
    d = device(&b, &c);
    b.model_clock = b.external_clock = true;
    assert(!tfa_dsp_start(&d, 7, 4));
    assert(tfa_dsp_check(&d, 7, 4) == 0);
    b.reg[0] &= ~4; /* Overtemperature: stop, then recover the requested tuning. */
    assert(tfa_dsp_check(&d, 7, 4) == -EAGAIN);
    assert(!d.powered && (b.reg[9] & 1));
    assert(tfa_dsp_check(&d, 7, 4) == -EAGAIN); /* Still hot: remain muted/off. */
    assert(!d.powered && !(b.reg[9] & 0x18));
    b.reg[0] |= 4;
    assert(tfa_dsp_check(&d, 7, 4) == 1);
    assert(d.powered && d.profile == 7 && d.vstep == 4);
    b.reg[0] |= 0x20; /* Latched OCP requires powerdown/up, not a cold DSP reload. */
    assert(tfa_dsp_check(&d, 7, 4) == -EAGAIN);
    assert(tfa_dsp_check(&d, 7, 4) == -EAGAIN);
    assert(!d.powered && d.configured && !(b.reg[9] & 0x18));
    b.clear_ocp_on_powerup = true;
    assert(tfa_dsp_check(&d, 7, 4) == 1);
    b.reg[0] &= ~0x10; /* Undervoltage must be handled even when CLKS is absent. */
    b.external_clock = false;
    assert(tfa_dsp_check(&d, 7, 4) == -EAGAIN);
    assert(!d.powered);
    b.reg[0] |= 0x10;
    assert(tfa_dsp_check(&d, 7, 4) == -EAGAIN);
    assert(!d.powered && (b.reg[9] & 1)); /* No clock: probe must not leave it on. */
    b.external_clock = true;
    assert(tfa_dsp_check(&d, 7, 4) == 1);
    b.reg[9] |= 1; /* Hardware powerdown differs from our cached state. */
    assert(tfa_dsp_check(&d, 7, 4) == 1);
    b.reg[0] |= 0x2800;
    assert(tfa_dsp_check(&d, 7, 4) == 1);
    assert(d.powered && d.configured && d.profile == 7 && d.vstep == 4);
    b.reg[9] &= ~0x20; /* Lost SBSL without a cold-status bit. */
    assert(tfa_dsp_check(&d, 7, 4) == 1);
    b.fault_after = 0;
    assert(tfa_dsp_check(&d, 7, 4) == -ENXIO);
    reset(&b);
    d = device(&b, &c);
    d.software_volume = true;
    assert(!tfa_dsp_start(&d, 0, 14));
    b.reg[0] |= 0x2800;
    assert(tfa_dsp_check(&d, 0, 14) == 1);
    assert(d.vstep == 14 && d.applied_software_volume && b.reg[6] >> 8 == 2);
    fprintf(stderr, "container, lifecycle, RPC and fault-injection checks passed\n");
    return 0;
}
