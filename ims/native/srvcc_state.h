// SPDX-License-Identifier: GPL-2.0
#pragma once
#include <stdint.h>

// Scheduler-thread-only state. A local timeout unfreezes updates but does not
// fabricate a CP outcome or forget a later real failure/cancellation.
class SrvccTableFreeze {
    bool pending = false;
    bool seen = false;
    int64_t deadline = 0;
public:
    bool onCpState(int state, bool hasCalls, int64_t now) {
        if (state == 0) {
            if (!pending || now >= deadline) {
                pending = seen = hasCalls;
                deadline = now + 10000;
            }
        } else if (state == 1) {
            pending = seen = false;
        } else if (state == 2 || state == 3) {
            bool notify = seen;
            pending = seen = false;
            return notify;
        }
        return false;
    }
    bool blocksUpdates(int64_t now) {
        if (pending && now >= deadline) pending = false;
        return pending;
    }
};
