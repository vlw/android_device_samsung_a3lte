/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the real HAL callbacks with a fake chip. No audio or telephony. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "../amplifier.c"

struct tfa_device {
    int unused;
};
static struct tfa_device chip;
static bool enabled;
static tfa_mode_t selected;
static unsigned int selected_step;
static int error;
static bool android_gain;
static const char *const names[] = {
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
tfa_device_t *tfa_device_open(void) { return &chip; }
void tfa_device_close(tfa_device_t *d)
{
    assert(d == &chip);
    enabled = false;
}
int tfa_apply(tfa_device_t *d, bool on, tfa_mode_t profile, unsigned int step)
{
    assert(d == &chip);
    enabled = on;
    selected = profile;
    selected_step = step;
    return error;
}
int tfa_apply_volume(tfa_device_t *d, bool on, tfa_mode_t profile, unsigned int step,
                     bool software_volume)
{
    android_gain = software_volume;
    return tfa_apply(d, on, profile, step);
}
const char *tfa_profile_name(tfa_device_t *d, unsigned int profile)
{
    assert(d == &chip);
    return profile < Audio_Mode_Max ? names[profile] : NULL;
}
int tfa_find_profile(tfa_device_t *d, const char *name)
{
    assert(d == &chip);
    for (int i = 0; i < Audio_Mode_Max; ++i)
        if (!strcmp(name, names[i])) return i;
    return -ENOENT;
}
unsigned int tfa_profile_steps(tfa_device_t *d, unsigned int profile)
{
    assert(d == &chip && profile < Audio_Mode_Max);
    return profile ? 6 : 15;
}
static int parameters(amplifier_device_t *a, const char *s)
{
    struct str_parms *p = str_parms_create_str(s);
    assert(p);
    int rc = a->set_parameters(a, p);
    str_parms_destroy(p);
    return rc;
}
int main(void)
{
    /* Regression for accidentally using the SM8350 platform enumeration. */
    _Static_assert(SND_DEVICE_OUT_VOICE_SPEAKER == 16, "MSM8916 route ABI changed");
    _Static_assert(SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES == 11, "MSM8916 combo ABI changed");
    hw_device_t *hw = NULL, *second = NULL;
    assert(!open_device(&HAL_MODULE_INFO_SYM.common, AMPLIFIER_HARDWARE_INTERFACE, &hw));
    assert(open_device(&HAL_MODULE_INFO_SYM.common, AMPLIFIER_HARDWARE_INTERFACE, &second) ==
           -EBUSY);
    amplifier_device_t *a = (amplifier_device_t *)hw;
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, true));
    assert(enabled && selected == Audio_Mode_Music_Normal);
    assert(!a->set_mode(a, AUDIO_MODE_IN_CALL));
    assert(enabled && selected == Audio_Mode_Voice_NB);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_VOICE_SPEAKER, false));
    assert(enabled); /* Disabling another route must not stop the active one. */
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, false));
    assert(!enabled);
    assert(!a->set_mode(a, AUDIO_MODE_NORMAL));
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_VOICE_SPEAKER, true));
    assert(enabled && selected == Audio_Mode_Voice_NB);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_HEADPHONES, true));
    assert(enabled);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, true));
    assert(selected == Audio_Mode_Voice_NB);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_VOICE_SPEAKER, false));
    assert(enabled && selected == Audio_Mode_Music_Normal);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, false));
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES, true));
    assert(enabled);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, false));
    assert(enabled);
    assert(!a->set_mode(a, AUDIO_MODE_IN_COMMUNICATION));
    assert(selected == Audio_Mode_VoIP);
    assert(!parameters(a, "tfa_profile=speechVoiceWB;tfa_vstep=5"));
    assert(selected == Audio_Mode_Voice_WB && selected_step == 5);
    assert(parameters(a, "tfa_vstep=6") == -EINVAL);
    assert(parameters(a, "tfa_vstep=-1") == -EINVAL);
    assert(parameters(a, "tfa_vstep=garbage") == -EINVAL);
    assert(parameters(a, "tfa_profile=not-a-profile") == -EINVAL);
    assert(selected == Audio_Mode_Voice_WB && selected_step == 5);
    assert(!parameters(a, "tfa_profile=auto;extra_volume=true"));
    assert(!a->set_mode(a, AUDIO_MODE_IN_CALL));
    assert(selected == Audio_Mode_Voice_NB_Extra && selected_step == 0);
    error = -EIO;
    assert(a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, true) == -EIO);
    error = 0;
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES, false));
    assert(enabled);
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, false));
    assert(!enabled);
    assert(!hw->close(hw));
    assert(!open_device(&HAL_MODULE_INFO_SYM.common, AMPLIFIER_HARDWARE_INTERFACE, &hw));
    a = (amplifier_device_t *)hw;
    assert(!parameters(a, "tfa_android_music=8,25;tfa_android_voice=5,7"));
    assert(!a->enable_output_devices(a, SND_DEVICE_OUT_SPEAKER, true));
    assert(android_gain && selected_step == 10);
    assert(!parameters(a, "tfa_android_music=25,25"));
    assert(selected_step == 0);
    assert(!parameters(a, "tfa_android_music=1,25"));
    assert(selected_step == 14);
    assert(!parameters(a, "tfa_android_music=0,25"));
    assert(selected_step == 14);
    assert(!parameters(a, "tfa_vstep=3"));
    assert(!android_gain && selected_step == 3);
    assert(!parameters(a, "tfa_android_music=25,25"));
    assert(!android_gain && selected_step == 3); /* Manual override survives publication. */
    assert(!parameters(a, "tfa_vstep=auto"));
    assert(android_gain && selected_step == 0);
    assert(parameters(a, "tfa_android_music=26,25") == -EINVAL);
    assert(parameters(a, "tfa_android_music=-1,25") == -EINVAL);
    assert(parameters(a, "tfa_android_music=1,0") == -EINVAL);
    assert(parameters(a, "tfa_android_music=1,25junk") == -EINVAL);
    assert(!a->set_mode(a, AUDIO_MODE_IN_CALL));
    assert(android_gain && selected_step == 1);
    assert(!a->set_mode(a, AUDIO_MODE_NORMAL));
    assert(android_gain && selected_step == 0);
    assert(!hw->close(hw));
    puts("HAL routing, mode transitions, parameter validation and reopen checks passed");
    return 0;
}
