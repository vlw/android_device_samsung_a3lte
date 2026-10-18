/* SPDX-License-Identifier: Apache-2.0 */
/* Isolated amplifier test: supplies only digital silence, never calls telephony. */
#include <cutils/str_parms.h>
#include <dlfcn.h>
#include <hardware/audio_amplifier.h>
#include <stdio.h>
#include <stdlib.h>

static int parameters(amplifier_device_t *a, const char *s)
{
    struct str_parms *p = str_parms_create_str(s);
    int rc = a->set_parameters(a, p);
    str_parms_destroy(p);
    return rc;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    hw_module_t *module = dlsym(lib, HAL_MODULE_INFO_SYM_AS_STR);
    hw_device_t *hw = NULL;
    int rc = module ? module->methods->open(module, AMPLIFIER_HARDWARE_INTERFACE, &hw) : -1;
    if (rc) {
        fprintf(stderr, "open failed: %d\n", rc);
        dlclose(lib);
        return 1;
    }
    amplifier_device_t *a = (amplifier_device_t *)hw;
    /* MSM8916 internal speaker route (also compile-time checked by hal_test.c). */
    rc = a->enable_output_devices(a, 2, true);
    fprintf(stderr, "music start: %d\n", rc);
    static const char *const profiles[] = {
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
        "hq",
    };
    for (unsigned int i = 0; !rc && i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        char command[96];
        snprintf(command, sizeof(command), "tfa_profile=%s;tfa_vstep=0", profiles[i]);
        rc = parameters(a, command);
        fprintf(stderr, "profile %s: %d\n", profiles[i], rc);
    }
    /* Exercise the real internal route numbers without creating a phone call. */
    if (!rc) rc = parameters(a, "tfa_profile=auto");
    if (!rc) rc = a->enable_output_devices(a, 16, true);
    if (!rc) rc = a->enable_output_devices(a, 2, false);
    if (!rc) rc = a->enable_output_devices(a, 2, true);
    if (!rc) rc = a->enable_output_devices(a, 16, false);
    fprintf(stderr, "speaker/voice-speaker route overlap and return: %d\n", rc);
    int stop_rc = a->enable_output_devices(a, 2, false);
    fprintf(stderr, "stop: %d\n", stop_rc);
    int close_rc = hw->close(hw);
    dlclose(lib);
    return rc || stop_rc || close_rc ? 1 : 0;
}
