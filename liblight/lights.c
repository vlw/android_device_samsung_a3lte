/*
 * Copyright (C) 2014-2015 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


//#define LOG_NDEBUG 0
#define LOG_TAG "lights"

#include <cutils/log.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/types.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t bl_g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t bt_g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_notification;
static struct light_state_t g_battery;
static int g_attention = 0;

const char *const RED_LED_FILE
        = "/sys/class/leds/red/brightness";

const char *const GREEN_LED_FILE
        = "/sys/class/leds/green/brightness";

const char *const BLUE_LED_FILE
        = "/sys/class/leds/blue/brightness";
const char *const LCD_FILE
        = "/sys/class/leds/lcd-backlight/brightness";
const char *const BUTTONS_FILE
        = "/sys/class/leds/button-backlight/brightness";

const char *const RED_BLINK_FILE
        = "/sys/class/leds/red/blink";

const char *const GREEN_BLINK_FILE
        = "/sys/class/leds/green/blink";

const char *const BLUE_BLINK_FILE
        = "/sys/class/leds/blue/blink";


/*modified by luohao for color led. (QL1700) 2014-07-12 begin*/
const char *const RED_LED_TIMER
        = "/sys/class/leds/red/red_timer";

const char *const GREEN_LED_TIMER
        = "/sys/class/leds/green/green_timer";

const char *const BLUE_LED_BLINK
        = "/sys/class/leds/blue/delay_on";

const char *const RED_LED_FLASH
        = "/sys/class/leds/red/red_tflash";

const char *const GREEN_LED_FLASH
        = "/sys/class/leds/green/green_tflash";

const char *const BLUE_LED_BLINK_OFF
        = "/sys/class/leds/blue/delay_off";

/*modified by luohao for color led (QL1700) 2014-07-12 end*/

/**
 * device methods
 */

void init_globals(void)
{
    // init the mutexes
    pthread_mutex_init(&bl_g_lock, NULL);
    pthread_mutex_init(&bt_g_lock, NULL);
}

static int
write_string(const char *path, const char *buffer)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        int bytes = strlen(buffer);
        int amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_string failed to open %s (%s)\n", path, strerror(errno));
            already_warned = 1;
        }
        return -errno;
    }
}

static int
write_int(const char *path, int value)
{
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%d\n", value);
    return write_string(path, buffer);
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(const struct light_state_t *state)
{
    int color = state->color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff))
            + (150 * ((color >> 8) & 0xff))
            + (29 * (color & 0xff))) >> 8;
}

static int
set_light_backlight(__attribute__ ((unused)) struct light_device_t *dev,
        const struct light_state_t *state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);
    if(!dev) {
        return -1;
    }

    pthread_mutex_lock(&bl_g_lock);

    err = write_int(LCD_FILE, brightness);

    pthread_mutex_unlock(&bl_g_lock);

    return err;
}
 void closeRed(){
  //write_int(RED_LED_TIMER_OFF, 0);
    write_int(RED_LED_TIMER, 0);
    write_int(RED_LED_FILE, 0);
}
 void closeGreen(){
  //write_int(GREEN_LED_BLINK_OFF, 0);
    write_int(GREEN_LED_TIMER, 0);
    write_int(GREEN_LED_FILE, 0);
}

 void closeBlue(){
  //write_int(BLUE_LED_BLINK_OFF, 0);
    write_int(BLUE_LED_BLINK, 0);
    write_int(BLUE_LED_FILE, 0);
}
void closeAllLeds(){
    write_int(RED_LED_TIMER, 0);
    write_int(RED_LED_FILE, 0);
    write_int(GREEN_LED_TIMER, 0);
    write_int(GREEN_LED_FILE, 0);
    write_int(BLUE_LED_BLINK, 0);
    write_int(BLUE_LED_FILE, 0);
}

static int
set_speaker_light_locked(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int red, green, blue;
    int blink;
    int onMS, offMS;
    unsigned int colorRGB;

    if(!dev) {
        return -1;
    }

    switch (state->flashMode) {
        case LIGHT_FLASH_TIMED:
            onMS = state->flashOnMS;
            offMS = state->flashOffMS;
            break;
        case LIGHT_FLASH_NONE:
        default:
            onMS = 0;
            offMS = 0;
            break;
    }

    colorRGB = state->color;

    ALOGD("set_speaker_light_locked mode %d, colorRGB=%08X, onMS=%d, offMS=%d\n",
            state->flashMode, colorRGB, onMS, offMS);

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    if (onMS > 0 && offMS > 0) {
        blink = 1;
    } else {
        blink = 0;
    }
    closeAllLeds();

	if (blink) {
		if(red) {
			write_int(RED_LED_FLASH, offMS);
			write_int(RED_LED_TIMER, onMS);
		} 
		if(green) {
			write_int(GREEN_LED_FLASH, offMS);
			write_int(GREEN_LED_TIMER, onMS);
		} 
		if(blue) {
			write_int(BLUE_LED_BLINK_OFF, offMS);
			write_int(BLUE_LED_BLINK, onMS);
		} 
	} else {
		if(red){
			write_int(RED_LED_FILE, red);
		}
		if(green){
			write_int(GREEN_LED_FILE, green);
		}
		if(blue){
			write_int(BLUE_LED_FILE, blue);
		}
	}
    return 0;
}

static void
handle_speaker_battery_locked(struct light_device_t* dev)
{
    if (is_lit(&g_battery)) {
        ALOGD("set battery ligth");
        set_speaker_light_locked(dev, &g_battery);
    } else {
        ALOGD("set notification ligth");
        set_speaker_light_locked(dev, &g_notification);
    }
}

static int
set_light_battery(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&bl_g_lock);
    g_battery = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&bl_g_lock);
    return 0;
}

static int
set_light_notifications(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&bl_g_lock);
    g_notification = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&bl_g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&bl_g_lock);
    if (state->flashMode == LIGHT_FLASH_HARDWARE) {
        g_attention = state->flashOnMS;
    } else if (state->flashMode == LIGHT_FLASH_NONE) {
        g_attention = 0;
    }
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&bl_g_lock);
    return 0;
}

static int
set_light_buttons(__attribute__ ((unused)) struct light_device_t *dev,
        const struct light_state_t *state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&bt_g_lock);

    err = write_int(BUTTONS_FILE, brightness);

    pthread_mutex_unlock(&bt_g_lock);

    return err;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}

/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *dev,
            const struct light_state_t *state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_battery;
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_notifications;
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name))
        set_light = set_light_attention;
    else
        return -EINVAL;

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));

    if(!dev)
        return -ENOMEM;

    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Jalebi Lights Module",
    .author = "The CyanogenMod Project",
    .methods = &lights_module_methods,
};
