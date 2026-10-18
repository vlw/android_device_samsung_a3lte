/* SPDX-License-Identifier: Apache-2.0 */
#include "tfa_container.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

uint16_t tfa_le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }

uint32_t tfa_le32(const uint8_t *p) { return tfa_le16(p) | (uint32_t)tfa_le16(p + 2) << 16; }

static uint32_t crc32(const uint8_t *p, size_t size)
{
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *p++;
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool range(const struct tfa_container *c, size_t offset, size_t size)
{
    return offset <= c->image.size && size <= c->image.size - offset;
}

static const char *string(const struct tfa_container *c, uint32_t descriptor)
{
    size_t offset = descriptor & 0xffffff;
    if ((descriptor >> 24) != 3 || !range(c, offset, 1)) return NULL;
    const char *s = (const char *)c->image.data + offset;
    return memchr(s, 0, c->image.size - offset) ? s : NULL;
}

/* File descriptors contain a name, a 32-bit length, then a 36-byte header. */
static int file(const struct tfa_container *c, uint32_t descriptor, struct tfa_span *result,
                uint16_t *id)
{
    size_t offset = descriptor & 0xffffff;
    if (!range(c, offset, 8)) return -EINVAL;
    const uint8_t *d = c->image.data + offset;
    size_t size = tfa_le32(d + 4);
    if (!string(c, tfa_le32(d)) || size < 36 || !range(c, offset + 8, size)) return -EINVAL;
    const uint8_t *header = d + 8;
    /* VP2 headers in Samsung's container deliberately have size == 0. */
    if (tfa_le16(header) != 0x5056 && tfa_le16(header + 6) != size) return -EINVAL;
    if (crc32(header + 12, size - 12) != tfa_le32(header + 8)) return -EBADMSG;
    *id = tfa_le16(header);
    result->data = header;
    result->size = size;
    return 0;
}

static int profile(struct tfa_container *c, uint32_t descriptor, unsigned int index)
{
    size_t offset = descriptor & 0xffffff;
    if (index >= TFA_PROFILE_COUNT || !range(c, offset, 8)) return -EINVAL;
    const uint8_t *p = c->image.data + offset;
    unsigned int length = p[0];
    /* Version 1 counts the name among the profile items (not device items). */
    if (length < 2 || tfa_le16(p + 1) != 0x1234 || !range(c, offset + 4, (size_t)length * 4))
        return -EINVAL;
    struct tfa_profile *prof = &c->profiles[index];
    prof->name = string(c, tfa_le32(p + 4));
    if (!prof->name) return -EINVAL;
    for (unsigned int i = 1; i < length; ++i) {
        uint32_t item = tfa_le32(p + 4 + i * 4);
        struct tfa_span span;
        uint16_t id;
        if ((item >> 24) != 4) return -ENOTSUP;
        int rc = file(c, item, &span, &id);
        if (rc) return rc;
        if (id == 0x5056) { /* VP2: attenuation, preset and ten biquads per step. */
            if (prof->vsteps.data || span.size < 38 || memcmp(span.data + 2, "2_01", 4))
                return -EINVAL;
            prof->steps = span.data[36];
            if (!prof->steps || span.data[37] != 8 ||
                span.size != 38 + (size_t)prof->steps * TFA_VSTEP_BYTES)
                return -EINVAL;
            prof->vsteps = span;
            for (unsigned int step = 0; step < prof->steps; ++step) {
                const uint8_t *v = span.data + 38 + step * TFA_VSTEP_BYTES;
                uint32_t bits = tfa_le32(v);
                float attenuation;
                memcpy(&attenuation, &bits, sizeof(attenuation));
                if (!isfinite(attenuation) || attenuation > 0 || attenuation < -127.5f)
                    return -EINVAL;
                for (unsigned int filter = 0; filter < 10; ++filter)
                    if (v[4 + TFA_PRESET_BYTES + filter * TFA_FILTER_BYTES + 18] > 1)
                        return -EINVAL;
            }
        } else if (id == 0x5244) { /* DR: the second 381-byte block is the DSP data. */
            if (prof->drc.data || span.size != 36 + 2 * 381) return -EINVAL;
            prof->drc = span;
        } else {
            return -ENOTSUP;
        }
    }
    return prof->vsteps.data && prof->drc.data ? 0 : -EINVAL;
}

int tfa_container_parse(struct tfa_container *c, const void *data, size_t size)
{
    static const char *const names[TFA_PROFILE_COUNT] = {
        "hq",
        "speechVoiceNB",
        "speechVoiceNBExtra",
        "speechVoiceWB",
        "speechVoiceWBExtra",
        "speechVTNB",
        "speechVTWB",
        "speechVoiceVOIP",
        "speechVoiceVoLTE",
        "speechVoiceVoLTEExtra",
        "speechVTVoLTE",
    };
    if (!c || !data || size < 52) return -EINVAL;
    memset(c, 0, sizeof(*c));
    c->image = (struct tfa_span){data, size};
    const uint8_t *b = data;
    if (memcmp(b, "PM1_00", 6) || tfa_le32(b + 6) != size || tfa_le16(b + 40) != 1 ||
        tfa_le16(b + 42) != TFA_PROFILE_COUNT || (tfa_le32(b + 44) >> 24) != 0)
        return -EINVAL;
    if (crc32(b + 14, size - 14) != tfa_le32(b + 10)) return -EBADMSG;
    size_t offset = tfa_le32(b + 44) & 0xffffff;
    if (!range(c, offset, 12)) return -EINVAL;
    const uint8_t *dev = b + offset;
    if (dev[1] != 0 || dev[2] != 0x34 || dev[3] != 0 || !string(c, tfa_le32(dev + 8)) ||
        !range(c, offset + 12, (size_t)dev[0] * 4))
        return -EINVAL;
    unsigned int profiles = 0;
    for (unsigned int i = 0; i < dev[0]; ++i) {
        uint32_t item = tfa_le32(dev + 12 + i * 4);
        unsigned int type = item >> 24;
        int rc;
        if (item & 0x80000000U) {
            if (c->field_count == sizeof(c->fields) / sizeof(c->fields[0])) return -E2BIG;
            c->fields[c->field_count++] = item & 0x7fffffffU;
        } else if (type == 1) {
            rc = profile(c, item, profiles++);
            if (rc) return rc;
        } else if (type == 4 || type == 5) {
            struct tfa_span span;
            uint16_t id;
            rc = file(c, item, &span, &id);
            if (rc) return rc;
            if (id == 0x4150 && type == 5 && !c->patch.data)
                c->patch = span;
            else if (id == 0x4f43 && type == 4 && !c->config.data)
                c->config = span;
            else if (id == 0x5053 && type == 4 && !c->speaker.data)
                c->speaker = span;
            else
                return -ENOTSUP;
        } else {
            return -ENOTSUP;
        }
    }
    if (profiles != TFA_PROFILE_COUNT || !c->patch.data || c->patch.size < 42 ||
        c->config.size != 237 || c->speaker.size != 496)
        return -EINVAL;
    for (unsigned int i = 0; i < TFA_PROFILE_COUNT; ++i)
        if (strcmp(c->profiles[i].name, names[i])) return -EINVAL;
    /* Validate every patch block before the first hardware write can happen. */
    for (size_t pos = 42; pos < c->patch.size;) {
        if (c->patch.size - pos < 2) return -EINVAL;
        size_t block = tfa_le16(c->patch.data + pos);
        pos += 2;
        if (block < 3 || block > 253 || block > c->patch.size - pos ||
            (c->patch.data[pos] != 0x70 && c->patch.data[pos] != 0x71))
            return -EINVAL;
        pos += block;
    }
    return 0;
}

int tfa_container_profile(const struct tfa_container *c, const char *name)
{
    if (!c || !name) return -EINVAL;
    for (unsigned int i = 0; i < TFA_PROFILE_COUNT; ++i)
        if (!strcmp(c->profiles[i].name, name)) return (int)i;
    return -ENOENT;
}
