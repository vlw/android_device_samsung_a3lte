/******************************************************************************
 *
 *  $Id: AKMD_APIs.h 1032 2013-06-12 09:23:43Z yamada.rj $
 *
 * -- Copyright Notice --
 *
 * Copyright (c) 2004 Asahi Kasei Microdevices Corporation, Japan
 * All Rights Reserved.
 *
 * This software program is the proprietary program of Asahi Kasei Microdevices
 * Corporation("AKM") licensed to authorized Licensee under the respective
 * agreement between the Licensee and AKM only for use with AKM's electronic
 * compass IC.
 *
 * THIS SOFTWARE IS PROVIDED TO YOU "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABLITY, FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT OF
 * THIRD PARTY RIGHTS, AND WE SHALL NOT BE LIABLE FOR ANY LOSSES AND DAMAGES
 * WHICH MAY OCCUR THROUGH USE OF THIS SOFTWARE.
 *
 * -- End Asahi Kasei Microdevices Copyright Notice --
 *
 ******************************************************************************/
#pragma once

#include "AKCommon.h"
#include "AKCompass.h"
#include "CalibrationModule.h"

#define AKMD_ERROR		-1
#define AKMD_SUCCESS	 0

#define ERROR_GET_SIZE_DOEP             (-5)
#define ERROR_MALLOC_DOEP               (-6)

/**
 * Define the Boolean macros only if they are not already defined.
 */
#ifndef __bool_true_false_are_defined
#define bool _Bool
#define false 0 
#define true 1
#define __bool_true_false_are_defined 1
#endif

#if defined(__cplusplus)
extern "C" {
#endif

int		AKMD_Init(void);

void	AKMD_Release(void);

int		AKMD_Start(void);

int		AKMD_Stop(void);

int	AKMD_GetData(
    const   int ix,
    const   int iy,
    const   int iz,
    const   int st2,
    const   int freq,
            float* ox,
            float* oy,
            float* oz,
            float* oxbias,
            float* oybias,
            float* ozbias,
            int* accuracy
);

int AKMD_GetVirtualSensors(sensors_event_t *raw_h, sensors_event_t *acc,
					 sensors_event_t *pg, sensors_event_t *rv ,
					 sensors_event_t *or, sensors_event_t *ga,
					 sensors_event_t *la, int dt);

void	AKMD_ResetDOE(void);

int		AKMD_ChangeFormation(int form);

#if defined(__cplusplus)
}
#endif


