/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A3_TFA_VOLUME_H
#define A3_TFA_VOLUME_H

/* Samsung policy sends maxIndex - index. Adapt its 15 music / 6 speech
 * steps to this ROM's configurable Android ranges, keeping both endpoints.
 * Music index zero is mute, not an additional (16th) DSP preset. */
static inline unsigned int tfa_volume_step(unsigned int index, unsigned int maximum,
                                           unsigned int steps)
{
    if (!maximum || steps < 2 || index >= maximum) return 0;
    unsigned int range = maximum - (steps > 6 && maximum > 1 ? 1 : 0);
    unsigned int step = ((maximum - index) * (steps - 1) + range / 2) / range;
    return step < steps ? step : steps - 1;
}

#endif
