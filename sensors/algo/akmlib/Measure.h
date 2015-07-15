/******************************************************************************
 *
 *  $Id: Measure.h 881 2012-11-30 09:30:41Z yamada.rj $
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
#ifndef AKMD_INC_MEASURE_H
#define AKMD_INC_MEASURE_H

// Include files for SmartCompass Library.
#include "AKCompass.h"

/*** Constant definition ******************************************************/
#define AKRET_PROC_SUCCEED      0x00	/*!< The process has been successfully done. */
#define AKRET_FORMATION_CHANGED 0x01	/*!< The formation is changed */
#define AKRET_DATA_READERROR    0x02	/*!< Data read error occurred. */
#define AKRET_DATA_OVERFLOW     0x04	/*!< Data overflow occurred. */
#define AKRET_OFFSET_OVERFLOW	0x08	/*!< Offset values overflow. */
#define AKRET_HBASE_CHANGED	0x10	/*!< hbase was changed. */
#define AKRET_HFLUC_OCCURRED    0x20	/*!< A magnetic field fluctuation occurred. */
#define AKRET_VNORM_ERROR	0x40	/*!< AKSC_VNorm error. */
#define AKRET_PROC_FAIL         0x80	/*!< The process failes. */

#define AKMD_MAG_MIN_INTERVAL	 10000000	/*!< Minimum magnetometer interval */
#define AKMD_LOOP_MARGIN	  3000000	/*!< Minimum sleep time */
#define AKMD_SETTING_INTERVAL	500000000	/*!< Setting event interval */

// 1/64
#define DISP_CONV_Q6F(val)		((val)*0.015625f)

/* deg x (pi/180.0) */
#define DEG2RAD(x)      ((AKSC_FLOAT)(((x) * AKSC_PI) / 180.0))
#define AKSC2SI(x)		((AKSC_FLOAT)(((x) * 9.80665f) / 720.0))


/*** Type declaration *********************************************************/
typedef int16(*OPEN_FORM)(void);
typedef void(*CLOSE_FORM)(void);
typedef int16(*CHECK_FORM)(void);

typedef struct _FORM_CLASS {
	OPEN_FORM	open;
	CLOSE_FORM	close;
	CHECK_FORM	check;
} FORM_CLASS;

/*** Global variables *********************************************************/

/*** Prototype of function ****************************************************/
void RegisterFormClass(
	FORM_CLASS *pt
);

void InitAKSCPRMS(
	AKSCPRMS	*prms
);

void SetDefaultPRMS(
	AKSCPRMS	*prms
);

int16 Init_Measure(
	AKSCPRMS	*prms
);

int16 GetMagneticVector(
	const int16	bData[],
	AKSCPRMS	*prms,
	const int16	curForm,
	const int16	hDecimator
);

int16 CalcDirection(
	AKSCPRMS* prms
);
int16 CalcAngularRate(
	AKSCPRMS* prms
);

#endif

