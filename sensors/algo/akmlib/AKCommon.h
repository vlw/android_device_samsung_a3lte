/******************************************************************************
 *
 *  $Id: AKCommon.h 1032 2013-06-12 09:23:43Z yamada.rj $
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
#ifndef AKMD_INC_AKCOMMON_H
#define AKMD_INC_AKCOMMON_H

#ifdef WIN32
#include <windows.h>
#include <stdio.h>	//fopen, snprint
#include <string.h>	//strncmp
#define snprintf	_snprintf
#define strlcpy		strncpy

#else
#include <stdio.h>     //frpintf
#include <stdlib.h>    //atoi
#include <string.h>    //memset
#include <unistd.h>
#include <stdarg.h>    //va_list
#include <utils/Log.h> //LOGV
#include <errno.h>     //errno
#include <sys/stat.h>  //chmod (in FileIO)

#endif

#ifndef ALOGV
#ifdef LOGV
#define ALOGV	LOGV
#else
#define ALOGV	printf
#endif
#endif

#ifndef ALOGD
#ifdef LOGD
#define ALOGD	LOGD
#else
#define ALOGD	printf
#endif
#endif

#ifndef ALOGI
#ifdef LOGI
#define ALOGI	LOGI
#else
#define ALOGI	printf
#endif
#endif

#ifndef ALOGW
#ifdef LOGW
#define ALOGW	LOGW
#else
#define ALOGW	printf
#endif
#endif

#ifndef ALOGE
#ifdef LOGE
#define ALOGE	LOGE
#else
#define ALOGE	printf
#endif
#endif

/*** Constant definition ******************************************************/
#undef LOG_TAG
#define LOG_TAG "AKMD2"

#define DATA_AREA01	0x0001
#define DATA_AREA02	0x0002
#define DATA_AREA03	0x0004
#define DATA_AREA04	0x0008
#define DATA_AREA05	0x0010
#define DATA_AREA06	0x0020
#define DATA_AREA07	0x0040
#define DATA_AREA08	0x0080
#define DATA_AREA09	0x0100
#define DATA_AREA10	0x0200
#define DATA_AREA11	0x0400
#define DATA_AREA12	0x0800
#define DATA_AREA13	0x1000
#define DATA_AREA14	0x2000
#define DATA_AREA15	0x4000
#define DATA_AREA16	0x8000


/* Debug area definition */

#define AKMDBG_LEVEL0		(DATA_AREA01)	/*<! Critical */
#define AKMDBG_LEVEL1		((DATA_AREA02) | (AKMDBG_LEVEL0))	/*<! Notice */
#define AKMDBG_LEVEL2		((DATA_AREA03) | (AKMDBG_LEVEL1))	/*<! Information */
#define AKMDBG_LEVEL3		((DATA_AREA04) | (AKMDBG_LEVEL2))	/*<! Debug */

#ifndef AKMDBG_LEVEL
#define AKMDBG_LEVEL		(AKMDBG_LEVEL3)
#endif

#ifndef ENABLE_AKMDEBUG
#define ENABLE_AKMDEBUG		(0)	/* Eanble debug output when it is 1. */
#endif

/* Definition for operation mode */
#define OPMODE_CONSOLE		(0x01)
#define OPMODE_DIRECTION	(0x02)

/***** Debug output ******************************************/
#if ENABLE_AKMDEBUG
#define AKMDEBUG(flag, format, ...) \
	((((int)flag) & AKMDBG_LEVEL) \
	  ? (ALOGD((format), ##__VA_ARGS__)) \
	  : ((void)0))
#else
#define AKMDEBUG(flag, format, ...)
#endif

/***** Log output ********************************************/
#ifdef AKM_LOG_ENABLE
#define AKM_LOG(format, ...)	ALOGD((format), ##__VA_ARGS__)
#else
#define AKM_LOG(format, ...)
#endif

/***** Error output *******************************************/
#define AKMERROR \
	  (ALOGE("%s:%d Error.", __FUNCTION__, __LINE__))

#define AKMERROR_STR(api) \
	  (ALOGE("%s:%d %s Error (%s).", \
	  		  __FUNCTION__, __LINE__, (api), strerror(errno)))

/*** Type declaration *********************************************************/

/*** Prototype of function ****************************************************/

#endif //AKMD_INC_AKCOMMON_H

