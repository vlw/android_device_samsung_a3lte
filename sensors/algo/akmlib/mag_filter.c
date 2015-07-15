#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "AKMD_APIs.h"

#define true    1
#define false   0

#define STDEV_THRESHOLD_ACC_X		0.09f
#define STDEV_THRESHOLD_ACC_Y		0.09f
#define STDEV_THRESHOLD_ACC_Z		0.09f
#define BUFFER_SIZE 64

static int acc_buf_pos = 0;
static int acc_buf_full = 0;
static int init_flag = false;
static float acc_x[BUFFER_SIZE],acc_y[BUFFER_SIZE],acc_z[BUFFER_SIZE];
enum
{
	ACC_SENSOR,
	ORI_SENSOR
};
void init_variables(void)
{
	int i;

	acc_buf_pos = 0;
	acc_buf_full = 0;
	for(i=0; i < BUFFER_SIZE; i++)
	{
		acc_x[i] = 0.0f;
		acc_y[i] = 0.0f;
		acc_z[i] = 0.0f;
		
	}
	init_flag = true;
}
static float cal_average(int sensor, const float data[])
{
	int i,nums;
	float ave,sum;

	if(ACC_SENSOR == sensor)
	{
		nums = (acc_buf_full ? BUFFER_SIZE : acc_buf_pos);
	}
	
	for(i=0; i<nums; i++)
	{
		sum += data[i];
	}
	return sum/nums;
}

static float cal_stdevp(int sensor, const float data[])
{
	int i = 0, num = 0;
    float sum = 0;
    float data_ave = 0;
    float var_sum = 0;
    float result = 0;

	if(ACC_SENSOR == sensor)
	{
		num = (acc_buf_full ? BUFFER_SIZE:acc_buf_pos); 
	//	ALOGE("AKM Debug acc buffer data = %d\n",num);
	}
    
    for(i=0; i<num; i++)
    {
        sum += data[i];
    }
    data_ave = sum/num;
    for(i=0; i<num; i++)
    {
        var_sum += (data[i]-data_ave)*(data[i]-data_ave);
    }
    result = sqrt(var_sum/num);

    return result;
}
void update_buf(float gx, float gy, float gz)
{
	acc_x[acc_buf_pos] = gx;
	acc_y[acc_buf_pos] = gy;
	acc_z[acc_buf_pos] = gz;

	acc_buf_pos++;
	if(acc_buf_pos >= BUFFER_SIZE)
	{
		acc_buf_pos = 0;
		acc_buf_full = 1;
	}
}

int detect_phone_moving(void)
{
	float stdev_val;
	
	stdev_val = cal_stdevp(ACC_SENSOR,acc_x);
//	ALOGE("AKM Debug stdev_val_x, %f\n",stdev_val);
	if(stdev_val > STDEV_THRESHOLD_ACC_X)
	{
		return 1;
	}
	stdev_val = cal_stdevp(ACC_SENSOR,acc_y);
//	ALOGE("AKM Debug stdev_val_y, %f\n",stdev_val);
	if(stdev_val > STDEV_THRESHOLD_ACC_Y)
	{
		return 1;
	}
	stdev_val = cal_stdevp(ACC_SENSOR,acc_z);
//	ALOGE("AKM Debug stdev_val_z, %f\n",stdev_val);
	if(stdev_val > STDEV_THRESHOLD_ACC_Z)
	{
		return 1;
	}
	return 0;
}

int Mag_filter(const sensors_event_t *acc)
{
	int buffer_cur_pos;
	float yaw,gx,gy,gz;
	float yaw_stdev;
	
	if(init_flag == false)
	{
//		ALOGE("AKM Debug init_flag %d\n",init_flag);
		init_variables();
	}
	gx = acc->acceleration.x;
	gy = acc->acceleration.y;
	gz = acc->acceleration.z;
	update_buf(gx,gy,gz);
	if(detect_phone_moving())
	{
//		ALOGE("AKM Debug phone moving====");
		return 1; 
	}
	else
	{
		return 0;
	}
}








