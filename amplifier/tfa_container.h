/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A3_TFA_CONTAINER_H
#define A3_TFA_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#define TFA_PROFILE_COUNT 11
#define TFA_PRESET_BYTES 87
#define TFA_FILTER_BYTES 32
#define TFA_VSTEP_BYTES 411

struct tfa_span {
    const uint8_t *data;
    size_t size;
};

struct tfa_profile {
    const char *name;
    struct tfa_span vsteps;
    struct tfa_span drc;
    unsigned int steps;
};

struct tfa_container {
    /* All spans point into the caller-owned, immutable image. */
    struct tfa_span image;
    struct tfa_span patch;
    struct tfa_span config;
    struct tfa_span speaker;
    uint32_t fields[16];
    unsigned int field_count;
    struct tfa_profile profiles[TFA_PROFILE_COUNT];
};

uint16_t tfa_le16(const uint8_t *p);
uint32_t tfa_le32(const uint8_t *p);
int tfa_container_parse(struct tfa_container *container, const void *data, size_t size);
int tfa_container_profile(const struct tfa_container *container, const char *name);

#endif
