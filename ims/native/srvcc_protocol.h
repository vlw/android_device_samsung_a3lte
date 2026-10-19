// SPDX-License-Identifier: GPL-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>

// Samsung IPC 4.1 voice call list: type LE16, id, direction, state,
// multiparty, number length, number type, ASCII number. Max 8 calls / 82 digits.
inline bool validateSrvccCalls(const uint8_t* body, size_t length, unsigned count) {
    if (count > 8 || length > 720) return false;
    size_t pos = 0;
    unsigned ids = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (length - pos < 8) return false;
        const auto* call = body + pos;
        if (call[0] != 5 || call[1] != 1 || call[2] < 1 || call[2] > 8 ||
            (ids & (1u << call[2])) || call[3] < 1 || call[3] > 2 ||
            call[4] < 1 || call[4] > 6 || call[5] > 1 || call[6] > 82 ||
            call[7] != 33 || length - pos - 8 < call[6]) return false;
        ids |= 1u << call[2];
        for (unsigned n = 0; n < call[6]; ++n) {
            char c = call[8 + n];
            if (!(c >= '0' && c <= '9') && c != '+' && c != '*' && c != '#') return false;
        }
        pos += 8 + call[6];
    }
    return pos == length;
}
