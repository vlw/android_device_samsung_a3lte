/*--------------------------------------------------------------------------
Copyright (c) 2014, The Linux Foundation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#include <stdio.h>
#include "AKMD_APIs.h"
#include "mag_filter.h"
#define SENSOR_CAL_ALGO_VERSION 1
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static int acc_en = 0;
struct sensor_cal_module_t SENSOR_CAL_MODULE_INFO;
static struct sensor_cal_algo_t algo_list[];

static sensors_event_t mag;
static sensors_event_t acc;
static int g_freq = 100;
//static int phone_moving = 0;
//sensors_event_t preHalval;
static int config_magnetic(int cmd, struct sensor_algo_args *args)
{	
	int ret = 0;
	struct compass_algo_args *param = (struct compass_algo_args*)args;

	switch (cmd) {		
		case CMD_ENABLE:
			if(param->common.enable) {
				if(ret = AKMD_Start())
					AKMERROR;
			}else {
			    if(ret = AKMD_Stop())
					AKMERROR;
			}
			break;
		case CMD_DELAY:
			if(param->common.delay_ms)
				g_freq = 1000 / param->common.delay_ms;
			break;
		case CMD_BATCH:
			break;
	}

	return ret;
}
static int config_accelerometer(int cmd, struct sensor_algo_args *args)
{
	acc_en = args->enable;
//	ALOGE("akm log acc_en = %d\n",acc_en);
	return 0;
}

static int convert_magnetic(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	int retValue = 0;
	float ox, oy, oz, oxbias, oybias, ozbias;
	int accuracy;
	sensors_event_t magtmp;
	magtmp = *raw;
	if(retValue = AKMD_GetData(magtmp.magnetic.x, magtmp.magnetic.y, magtmp.magnetic.z, 0, g_freq,
			&ox, &oy, &oz, &oxbias, &oybias, &ozbias, &accuracy)) {

			ALOGE("akmd_get data error:%d\n", retValue);
			return retValue;
	}
	result->magnetic.x = ox;
	result->magnetic.y = oy;
	result->magnetic.z = oz;
	result->magnetic.status = accuracy;
	return 0;
}

static int convert_accelerometer(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	//phone_moving = Mag_filter(raw);
	return 0;
}

static int get_orientation(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	int ret = 0;
	int dt = 0;
	static int64_t timestamp = 0;
//	ALOGE("akm_log get_orientation=====\n");
	if(raw->type == SENSOR_TYPE_MAGNETIC_FIELD) {
		mag.magnetic.x = raw->magnetic.x;
		mag.magnetic.y = raw->magnetic.y;
		mag.magnetic.z = raw->magnetic.z;
		dt = (raw->timestamp - timestamp) / 1000 /1000;
		timestamp = raw->timestamp;
	}


	if(raw->type == SENSOR_TYPE_ACCELEROMETER) {
		//phone_moving = Mag_filter(raw);
		acc.acceleration.x = raw->acceleration.x;
		acc.acceleration.y = raw->acceleration.y;
		acc.acceleration.z = raw->acceleration.z;
		return -1;
	}

	ret = AKMD_GetVirtualSensors(&mag, &acc, NULL, NULL, result, NULL, NULL, dt);
	if(ret)
		ALOGV("akm get virtual sensor error :%d\n", ret);

	return ret;
}

static int get_rotation_vector(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	int ret = 0;
	int dt = 0;
	static int64_t timestamp = 0;

	if(raw->type == SENSOR_TYPE_MAGNETIC_FIELD) {
		mag.magnetic.x = raw->magnetic.x;
		mag.magnetic.y = raw->magnetic.y;
		mag.magnetic.z = raw->magnetic.z;
		dt = (raw->timestamp - timestamp) / 1000 /1000;
		timestamp = raw->timestamp;
	}


	if(raw->type == SENSOR_TYPE_ACCELEROMETER) {
		acc.acceleration.x = raw->acceleration.x;
		acc.acceleration.y = raw->acceleration.y;
		acc.acceleration.z = raw->acceleration.z;
		return -1;
	}


	ret = AKMD_GetVirtualSensors(&mag, &acc, NULL, result, NULL, NULL, NULL, dt);
	if(ret)
		ALOGV("akm get virtual sensor error :%d\n", ret);

	return ret;
}

static int get_virtual_gyro(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	int ret = 0;
	int dt = 0;
	static int64_t timestamp = 0;

	if(raw->type == SENSOR_TYPE_MAGNETIC_FIELD) {
		mag.magnetic.x = raw->magnetic.x;
		mag.magnetic.y = raw->magnetic.y;
		mag.magnetic.z = raw->magnetic.z;
		dt = (raw->timestamp - timestamp) / 1000 / 1000;
		timestamp = raw->timestamp;
	}


	if(raw->type == SENSOR_TYPE_ACCELEROMETER) {
		acc.acceleration.x = raw->acceleration.x;
		acc.acceleration.y = raw->acceleration.y;
		acc.acceleration.z = raw->acceleration.z;
		return -1;
	}


	ret = AKMD_GetVirtualSensors(&mag, &acc, result, NULL, NULL, NULL, NULL, dt);
	if(ret)
		ALOGV("akm get virtual sensor error :%d\n", ret);

	return ret;
}

static int get_gravity(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	int ret = 0;
	int dt = 0;
	static int64_t timestamp = 0;

	if(raw->type == SENSOR_TYPE_MAGNETIC_FIELD) {
		mag.magnetic.x = raw->magnetic.x;
		mag.magnetic.y = raw->magnetic.y;
		mag.magnetic.z = raw->magnetic.z;
		dt = (raw->timestamp - timestamp) / 1000 /1000;
		timestamp = raw->timestamp;
	}


	if(raw->type == SENSOR_TYPE_ACCELEROMETER) {
		acc.acceleration.x = raw->acceleration.x;
		acc.acceleration.y = raw->acceleration.y;
		acc.acceleration.z = raw->acceleration.z;
		return -1;
	}


	ret = AKMD_GetVirtualSensors(&mag, &acc, NULL, NULL, NULL, result, NULL, dt);
	if(ret)
		ALOGV("akm get virtual sensor error :%d\n", ret);

	return ret;
}

static int get_la(sensors_event_t *raw, sensors_event_t *result, struct sensor_algo_args *args)
{
	int ret = 0;
	int dt = 0;
	static int64_t timestamp = 0;

	if(raw->type == SENSOR_TYPE_MAGNETIC_FIELD) {
		mag.magnetic.x = raw->magnetic.x;
		mag.magnetic.y = raw->magnetic.y;
		mag.magnetic.z = raw->magnetic.z;
		dt = (raw->timestamp - timestamp) / 1000 /1000;
		timestamp = raw->timestamp;
	}


	if(raw->type == SENSOR_TYPE_ACCELEROMETER) {
		acc.acceleration.x = raw->acceleration.x;
		acc.acceleration.y = raw->acceleration.y;
		acc.acceleration.z = raw->acceleration.z;
		return -1;
	}


	ret = AKMD_GetVirtualSensors(&mag, &acc, NULL, NULL, NULL, NULL, result, dt);
	if(ret)
		ALOGV("akm get virtual sensor error :%d\n", ret);

	return ret;
}

static int cal_init(const struct sensor_cal_module_t *module)
{
	AKMD_Init();

	return 0;
}

static void cal_deinit()
{
	AKMD_Release();
}

static int cal_get_algo_list(const struct sensor_cal_algo_t **algo)
{
	*algo = algo_list;
	return 0;
}

static struct sensor_algo_methods_t algo_methods = {
	.convert = convert_magnetic,
	.config = config_magnetic,
};

static struct sensor_algo_methods_t acc_methods = {
	.convert = convert_accelerometer,
	.config = config_accelerometer,//config_magnetic,
};
static struct sensor_algo_methods_t or_methods = {
	.convert = get_orientation,
	.config = NULL,
};

static struct sensor_algo_methods_t rv_methods = {
	.convert = get_rotation_vector,
	.config = NULL,
};

static struct sensor_algo_methods_t pg_methods = {
	.convert = get_virtual_gyro,
	.config = NULL,
};

static struct sensor_algo_methods_t gv_methods = {
	.convert = get_gravity,
	.config = NULL,
};

static struct sensor_algo_methods_t la_methods = {
	.convert = get_la,
	.config = NULL,
};

static const char* mag_match_table[] = {
	"akm09911-mag",
	NULL
};

static const char* acc_match_table[] = {
	"accelerometer",
	NULL
};
static const char* or_match_table[] = {
	"oem-orientation",
	NULL
};

static const char* rv_match_table[] = {
	"oem-rotation-vector",
	NULL
};

static const char* pg_match_table[] = {
	"oem-pseudo-gyro",
	NULL
};

static const char* gv_match_table[] = {
	"oem-gravity",
	NULL
};

static const char* la_match_table[] = {
	"oem-linear-acceleration",
	NULL
};

static struct sensor_cal_algo_t algo_list[] = {
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_MAGNETIC_FIELD,
		.compatible = mag_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &algo_methods,
},
#if 0
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_ACCELEROMETER,
		.compatible = acc_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &acc_methods,
	},
#endif
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_ORIENTATION,
		.compatible = or_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &or_methods,
	},
#ifdef ENABLE_AKM_PG
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_ROTATION_VECTOR,
		.compatible = rv_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &rv_methods,
	},
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_GYROSCOPE,
		.compatible = pg_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &pg_methods,
	},
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_GRAVITY,
		.compatible = gv_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &gv_methods,
	},
	{
		.tag = SENSOR_CAL_ALGO_TAG,
		.version = SENSOR_CAL_ALGO_VERSION,
		.type = SENSOR_TYPE_LINEAR_ACCELERATION,
		.compatible = la_match_table,
		.module = &SENSOR_CAL_MODULE_INFO,
		.methods = &la_methods,
	},
#endif
};

static struct sensor_cal_methods_t cal_methods = {
	.init = cal_init,
	.deinit = cal_deinit,
	.get_algo_list = cal_get_algo_list,
};

struct sensor_cal_module_t SENSOR_CAL_MODULE_INFO = {
	.tag = SENSOR_CAL_MODULE_TAG,
	.id = "cal_module_akm",
	.version = SENSOR_CAL_MODULE_VERSION,
	.vendor = "akm",
	.dso = NULL,
	.number = ARRAY_SIZE(algo_list),
	.methods = &cal_methods,
	.reserved = {0},
};
