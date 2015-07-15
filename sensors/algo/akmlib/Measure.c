
/******************************************************************************
 *
 *  $Id: Measure.c 1071 2013-08-30 06:58:03Z yamada.rj $
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
#include "Measure.h"

#if defined(AKMD_AK8975)
#define AKMD_DEVICE		8975
#define AKMD_ST2_POS	7
#elif defined(AKMD_AK8963)
#define AKMD_DEVICE		8963
#define AKMD_ST2_POS	7
#elif defined(AKMD_AK09912)
#define AKMD_DEVICE		9912
#define AKMD_ST2_POS	8
#elif defined(AKMD_AK09911)
#define AKMD_DEVICE		9911
#define AKMD_ST2_POS	8
#endif

static FORM_CLASS *g_form = NULL;

/*!
 This function open formation status device.
 @return Return 0 on success. Negative value on fail.
 */
static int16 openForm(void)
{
	if (g_form != NULL) {
		if (g_form->open != NULL) {
			return g_form->open();
		}
	}
	// If function is not set, return success.
	return 0;
}

/*!
 This function close formation status device.
 @return None.
 */
static void closeForm(void)
{
	if (g_form != NULL) {
		if (g_form->close != NULL) {
			g_form->close();
		}
	}
}

/*!
 This function check formation status
 @return The index of formation.
 */
static int16 checkForm(void)
{
	if (g_form != NULL) {
		if (g_form->check != NULL) {
			return g_form->check();
		}
	}
	// If function is not set, return default value.
	return 0;
}

/*!
 This function registers the callback function.
 @param[in]
 */
void RegisterFormClass(FORM_CLASS *pt)
{
	g_form = pt;
}

/*!
 Initialize #AKSCPRMS structure. At first, 0 is set to all parameters.
 After that, some parameters, which should not be 0, are set to specific
 value. Some of initial values can be customized by editing the file
 \c "CustomerSpec.h".
 @param[out] prms A pointer to #AKSCPRMS structure.
 */
void InitAKSCPRMS(AKSCPRMS *prms)
{
	AKMDEBUG(AKMDBG_LEVEL0, "AKMLib Ver: %d.%d.%d.%d\n",
		AKSC_GetVersion_Major(),
		AKSC_GetVersion_Minor(),
		AKSC_GetVersion_Revision(),
		AKSC_GetVersion_DateCode());
	AKMDEBUG(AKMDBG_LEVEL0, "AKM Device: %d\n", AKMD_DEVICE);

	// Set 0 to the AKSCPRMS structure.
	memset(prms, 0, sizeof(AKSCPRMS));

	// Sensitivity
	prms->m_hs.u.x = AKSC_HSENSE_TARGET;
	prms->m_hs.u.y = AKSC_HSENSE_TARGET;
	prms->m_hs.u.z = AKSC_HSENSE_TARGET;

	// HDOE
	prms->m_hdst = AKSC_HDST_UNSOLVED;

	// (m_hdata is initialized with AKSC_InitDecomp)
	prms->m_hnave = CSPEC_HNAVE;
	prms->m_dvec.u.x = CSPEC_DVEC_X;
	prms->m_dvec.u.y = CSPEC_DVEC_Y;
	prms->m_dvec.u.z = CSPEC_DVEC_Z;
}

/*!
 Fill #AKSCPRMS structure with default value.
 @param[out] prms A pointer to #AKSCPRMS structure.
 */
void SetDefaultPRMS(AKSCPRMS *prms)
{
	int16 i;
#ifndef AKMD_DISABLE_DOEPLUS
	int16 j;
#endif
	// Set parameter to HDST, HO, HREF
	for (i = 0; i < CSPEC_NUM_FORMATION; i++) {
		prms->HSUC_HDST[i] = AKSC_HDST_UNSOLVED;
		prms->HSUC_HO[i].u.x = 0;
		prms->HSUC_HO[i].u.y = 0;
		prms->HSUC_HO[i].u.z = 0;
		prms->HFLUCV_HREF[i].u.x = 0;
		prms->HFLUCV_HREF[i].u.y = 0;
		prms->HFLUCV_HREF[i].u.z = 0;
		prms->HSUC_HBASE[i].u.x = 0;
		prms->HSUC_HBASE[i].u.y = 0;
		prms->HSUC_HBASE[i].u.z = 0;
#ifndef AKMD_DISABLE_DOEPLUS
		for (j = 0; j < AKSC_DOEP_SIZE; j++) {
			prms->DOEP_PRMS[i][j] = (AKSC_FLOAT)(0.);
		}
#endif
	}
}

/*!
 Set initial values for SmartCompass library.
 @return If parameters are read successfully, the return value is
 #AKRET_PROC_SUCCEED. Otherwise the return value is #AKRET_PROC_FAIL. No
 error code is reserved to show which operation has failed.
 @param[in,out] prms A pointer to a #AKSCPRMS structure.
 */
int16 Init_Measure(AKSCPRMS *prms)
{
#if 0
	// Reset device.
	if (AKD_Reset() != AKD_SUCCESS) {
		AKMERROR;
		return AKRET_PROC_FAIL;
	}
#endif

	prms->m_form = checkForm();
	prms->m_curForm = prms->m_form;

	// Restore the value when succeeding in estimating of HOffset.
	prms->m_ho   = prms->HSUC_HO[prms->m_form];
	prms->m_ho32.u.x = (int32)prms->HSUC_HO[prms->m_form].u.x;
	prms->m_ho32.u.y = (int32)prms->HSUC_HO[prms->m_form].u.y;
	prms->m_ho32.u.z = (int32)prms->HSUC_HO[prms->m_form].u.z;

	prms->m_hdst = prms->HSUC_HDST[prms->m_form];
	prms->m_hbase = prms->HSUC_HBASE[prms->m_form];

	// Initialize the decompose parameters
	AKSC_InitDecomp(prms->m_hdata);

#ifndef AKMD_DISABLE_DOEPLUS
	// Initialize DOEPlus parameters
	AKSC_InitDOEPlus(prms->m_doep_var);
	prms->m_doep_lv = AKSC_LoadDOEPlus(
						prms->DOEP_PRMS[prms->m_form],
						prms->m_doep_var);
	AKSC_InitDecomp(prms->m_hdata_plus);
#endif

	// Initialize HDOE parameters
	AKSC_InitHDOEProcPrmsS3(
			&prms->m_hdoev,
			1,
			&prms->m_ho,
			prms->m_hdst
			);

	AKSC_InitHFlucCheck(
			&(prms->m_hflucv),
			&(prms->HFLUCV_HREF[prms->m_form]),
			HFLUCV_TH
			);

	AKSC_InitPseudoGyro(
						&prms->m_pgcond,
						&prms->m_pgvar
						);


	prms->m_pgcond.fmode = 1;
	prms->m_pgcond.ihave = 24;
	prms->m_pgcond.iaave = 24;
	prms->m_pgcond.ocoef = 307;
	prms->m_pgcond.th_rmax = 2400;
	prms->m_pgcond.th_rmin = 0;
	prms->m_pgcond.th_rdif = 2400;

	// Reset counter
	prms->m_cntSuspend = 0;
	prms->m_callcnt = 0;

	return AKRET_PROC_SUCCEED;
}

/*!
 SmartCompass main calculation routine. This function will be processed
 when INT pin event is occurred.
 @retval AKRET_
 @param[in] bData An array of register values which holds,
 ST1, HXL, HXH, HYL, HYH, HZL, HZH and ST2 value respectively.
 @param[in,out] prms A pointer to a #AKSCPRMS structure.
 @param[in] curForm The index of hardware position which represents the
 index when this function is called.
 @param[in] hDecimator HDOE will execute once while this function is called
 this number of times.
 */
int16 GetMagneticVector(
	const int16	bData[],
	AKSCPRMS	*prms,
	const int16	curForm,
	const int16	hDecimator)
{
	const int16vec hrefZero = {{0, 0, 0}};
	int16vec	have, hvec;
	int16		temperature, dor, derr, hofl, cb, dc;
	int32vec	preHbase;
	int16		overflow;
	int16		hfluc;
	int16		hdSucc;
	int16		aksc_ret;
	int16		ret;
#ifndef AKMD_DISABLE_DOEPLUS
    int16       i;
	int16		doep_ret;
#endif

	have.u.x = 0;
	have.u.y = 0;
	have.u.z = 0;
	temperature = 0;
	dor = 0;
	derr = 0;
	hofl = 0;
	cb = 0;
	dc = 0;

	preHbase = prms->m_hbase;
	overflow = 0;
	ret = AKRET_PROC_SUCCEED;

	// Subtract the formation suspend counter
	if (prms->m_cntSuspend > 0) {
		prms->m_cntSuspend--;

		// Check the suspend counter
		if (prms->m_cntSuspend == 0) {
			// Restore the value when succeeding in estimating of HOffset.
			prms->m_ho   = prms->HSUC_HO[prms->m_form];
			prms->m_ho32.u.x = (int32)prms->HSUC_HO[prms->m_form].u.x;
			prms->m_ho32.u.y = (int32)prms->HSUC_HO[prms->m_form].u.y;
			prms->m_ho32.u.z = (int32)prms->HSUC_HO[prms->m_form].u.z;

			prms->m_hdst = prms->HSUC_HDST[prms->m_form];
			prms->m_hbase = prms->HSUC_HBASE[prms->m_form];

			// Initialize the decompose parameters
			AKSC_InitDecomp(prms->m_hdata);

#ifndef AKMD_DISABLE_DOEPLUS
			// Initialize DOEPlus parameters
			AKSC_InitDOEPlus(prms->m_doep_var);
			prms->m_doep_lv = AKSC_LoadDOEPlus(
								prms->DOEP_PRMS[prms->m_form],
								prms->m_doep_var);
			AKSC_InitDecomp(prms->m_hdata_plus);
#endif

			// Initialize HDOE parameters
			AKSC_InitHDOEProcPrmsS3(
				&prms->m_hdoev,
				1,
				&prms->m_ho,
				prms->m_hdst
			);

			// Initialize HFlucCheck parameters
			AKSC_InitHFlucCheck(
				&(prms->m_hflucv),
				&(prms->HFLUCV_HREF[prms->m_form]),
				HFLUCV_TH
			);
		}
	}

	// Decompose one block data into each Magnetic sensor's data
	aksc_ret = AKSC_DecompS3(
					AKMD_DEVICE,
					bData,
					prms->m_hnave,
					&prms->m_asa,
					prms->m_pdcptr,
					prms->m_hdata,
					&prms->m_hbase,
					&prms->m_hn,
					&have,
					&temperature,
					&dor,
					&derr,
					&hofl,
					&cb,
					&dc
				);
	AKM_LOG("%s: ST1, HXH&HXL, HYH&HYL, HZH&HZL, ST2,"
			" hdata[0].u.x, hdata[0].u.y, hdata[0].u.z,"
			" asax, asay, asaz ="
			" %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
			__FUNCTION__,
			bData[0],
			(int16)(((uint16)bData[2])<<8|bData[1]),
			(int16)(((uint16)bData[4])<<8|bData[3]),
			(int16)(((uint16)bData[6])<<8|bData[5]), bData[AKMD_ST2_POS],
			prms->m_hdata[0].u.x, prms->m_hdata[0].u.y, prms->m_hdata[0].u.z,
			prms->m_asa.u.x, prms->m_asa.u.y, prms->m_asa.u.z);

	if (aksc_ret == 0) {
		AKMERROR;
		AKMDEBUG(AKMDBG_LEVEL0,
				"AKSC_DecompS3 failed.\n"
				"  ST1=0x%02X, ST2=0x%02X\n"
				"  XYZ(HEX)=%02X,%02X,%02X,%02X,%02X,%02X\n"
				"  asa(dec)=%d,%d,%d\n"
				"  pdc(addr)=0x%p\n"
				"  hbase(dec)=%ld,%ld,%ld\n",
				bData[0], bData[AKMD_ST2_POS],
				bData[1], bData[2], bData[3], bData[4], bData[5], bData[6],
				prms->m_asa.u.x, prms->m_asa.u.y, prms->m_asa.u.z,
				prms->m_pdcptr,
				prms->m_hbase.u.x, prms->m_hbase.u.y, prms->m_hbase.u.z);
		return AKRET_PROC_FAIL;
	}

	// Check the formation change
	if (prms->m_form != curForm) {
		prms->m_form = curForm;
		prms->m_cntSuspend = CSPEC_CNTSUSPEND_SNG;
		prms->m_callcnt = 0;
		ret |= AKRET_FORMATION_CHANGED;
		return ret;
	}

	// Check derr
	if (derr == 1) {
		ret |= AKRET_DATA_READERROR;
		return ret;
	}

	// Check hofl
	if (hofl == 1) {
		if (prms->m_cntSuspend <= 0) {
			// Set a HDOE level as "HDST_UNSOLVED"
			AKSC_SetHDOELevel(
							  &prms->m_hdoev,
							  &prms->m_ho,
							  AKSC_HDST_UNSOLVED,
							  1
							  );
			prms->m_hdst = AKSC_HDST_UNSOLVED;
		}
		ret |= AKRET_DATA_OVERFLOW;
		return ret;
	}

	// Check hbase
	if (cb == 1) {
		// Translate HOffset
		AKSC_TransByHbase(
			&(preHbase),
			&(prms->m_hbase),
			&(prms->m_ho),
			&(prms->m_ho32),
			&overflow
		);
		if (overflow == 1) {
			ret |= AKRET_OFFSET_OVERFLOW;
		}

		// Set hflucv.href to 0
		AKSC_InitHFlucCheck(
			&(prms->m_hflucv),
			&(hrefZero),
			HFLUCV_TH
		);

		if (prms->m_cntSuspend <= 0) {
			AKSC_SetHDOELevel(
				&prms->m_hdoev,
				&prms->m_ho,
				AKSC_HDST_UNSOLVED,
				1
			);
			prms->m_hdst = AKSC_HDST_UNSOLVED;
		}

		ret |= AKRET_HBASE_CHANGED;
		return ret;
	}

	if (prms->m_cntSuspend <= 0) {
		// Detect a fluctuation of magnetic field.
		hfluc = AKSC_HFlucCheck(&(prms->m_hflucv), &(prms->m_hdata[0]));

		if (hfluc == 1) {
			// Set a HDOE level as "HDST_UNSOLVED"
			AKSC_SetHDOELevel(
				&prms->m_hdoev,
				&prms->m_ho,
				AKSC_HDST_UNSOLVED,
				1
			);
			prms->m_hdst = AKSC_HDST_UNSOLVED;

			ALOGD("AKM_LOG return AKRET_HFLUC_OCCURRED\n");

			ret |= AKRET_HFLUC_OCCURRED;
			return ret;
		}
		else {
			prms->m_callcnt--;
			if (prms->m_callcnt <= 0) {
#ifndef AKMD_DISABLE_DOEPLUS
				// Compensate Magnetic Distortion by DOEPlus
				doep_ret = AKSC_DOEPlus(&prms->m_hdata[0], prms->m_doep_var, &prms->m_doep_lv);

					// Save DOEPlus parameters
					if ((doep_ret == 1) && (prms->m_doep_lv == 3)) {
						AKSC_SaveDOEPlus(prms->m_doep_var, prms->DOEP_PRMS[prms->m_form]);
					}

					// Calculate compensated vector for DOE
					for(i = 0; i < prms->m_hn; i++) {
						AKSC_DOEPlus_DistCompen(&prms->m_hdata[i], prms->m_doep_var, &prms->m_hdata_plus[i]);
					}

					AKMDEBUG(AKMDBG_LEVEL1,"DOEP: %7d, %7d, %7d ",
							prms->m_hdata[0].u.x,
							prms->m_hdata[0].u.y,
							prms->m_hdata[0].u.z);
					AKMDEBUG(AKMDBG_LEVEL1,"|%7d, %7d, %7d \n",
							prms->m_hdata_plus[0].u.x,
							prms->m_hdata_plus[0].u.y,
							prms->m_hdata_plus[0].u.z);

				//Calculate Magnetic sensor's offset by DOE
				hdSucc = AKSC_HDOEProcessS3(
							prms->m_licenser,
							prms->m_licensee,
							prms->m_key,
							&prms->m_hdoev,
							prms->m_hdata_plus,
							prms->m_hn,
							&prms->m_ho,
							&prms->m_hdst
							);
#else
				hdSucc = AKSC_HDOEProcessS3(
							prms->m_licenser,
							prms->m_licensee,
							prms->m_key,
							&prms->m_hdoev,
							prms->m_hdata,
							prms->m_hn,
							&prms->m_ho,
							&prms->m_hdst
							);
#endif

				if (hdSucc == AKSC_CERTIFICATION_DENIED) {
					AKMERROR;
					return AKRET_PROC_FAIL;
				}
				if (hdSucc > 0) {
					prms->HSUC_HO[prms->m_form] = prms->m_ho;
					prms->m_ho32.u.x = (int32)prms->m_ho.u.x;
					prms->m_ho32.u.y = (int32)prms->m_ho.u.y;
					prms->m_ho32.u.z = (int32)prms->m_ho.u.z;

					prms->HSUC_HDST[prms->m_form] = prms->m_hdst;
					prms->HFLUCV_HREF[prms->m_form] = prms->m_hflucv.href;
					prms->HSUC_HBASE[prms->m_form] = prms->m_hbase;
				}

				//Set decimator counter
				prms->m_callcnt = hDecimator;
			}
		}
	}

#ifndef AKMD_DISABLE_DOEPLUS
	// Calculate compensated vector
	AKSC_DOEPlus_DistCompen(&have, prms->m_doep_var, &have);
#endif

	// Subtract offset and normalize magnetic field vector.
	aksc_ret = AKSC_VNorm(
						  &have,
						  &prms->m_ho,
						  &prms->m_hs,
						  AKSC_HSENSE_TARGET,
						  &hvec
						  );
	if (aksc_ret == 0) {
		AKMERROR;
		AKMDEBUG(AKMDBG_LEVEL0,
				"AKSC_VNorm failed.\n"
				"  have=%6d,%6d,%6d  ho=%6d,%6d,%6d  hs=%6d,%6d,%6d\n",
				have.u.x, have.u.y, have.u.z,
				prms->m_ho.u.x, prms->m_ho.u.y, prms->m_ho.u.z,
				prms->m_hs.u.x, prms->m_hs.u.y, prms->m_hs.u.z);
		ret |= AKRET_VNORM_ERROR;
		return ret;
	}

	// hvec is updated only when VNorm function is succeeded.
	prms->m_hvec = hvec;

	// Bias of Uncalibrated Magnetic Field
	prms->m_bias.u.x = (int32)(prms->m_ho.u.x) + prms->m_hbase.u.x;
	prms->m_bias.u.y = (int32)(prms->m_ho.u.y) + prms->m_hbase.u.y;
	prms->m_bias.u.z = (int32)(prms->m_ho.u.z) + prms->m_hbase.u.z;

#if 0
	//Convert layout from sensor to Android by using PAT number.
	// Magnetometer
	ConvertCoordinate(prms->m_hlayout, &prms->m_hvec);
	// Bias of Uncalibrated Magnetic Field
	ConvertCoordinate32(prms->m_hlayout, &prms->m_bias);
#endif

	// Magnetic Field
	prms->m_calib.u.x = prms->m_hvec.u.x;
	prms->m_calib.u.y = prms->m_hvec.u.y;
	prms->m_calib.u.z = prms->m_hvec.u.z;

	// Uncalibrated Magnetic Field
	prms->m_uncalib.u.x = (int32)(prms->m_calib.u.x) + prms->m_bias.u.x;
	prms->m_uncalib.u.y = (int32)(prms->m_calib.u.y) + prms->m_bias.u.y;
	prms->m_uncalib.u.z = (int32)(prms->m_calib.u.z) + prms->m_bias.u.z;

	AKMDEBUG(AKMDBG_LEVEL3,
			"mag(dec)=%6d,%6d,%6d\n"
			"maguc(dec),bias(dec)=%7ld,%7ld,%7ld,%7ld,%7ld,%7ld\n",
			prms->m_calib.u.x, prms->m_calib.u.y, prms->m_calib.u.z,
			prms->m_uncalib.u.x, prms->m_uncalib.u.y, prms->m_uncalib.u.z,
			prms->m_bias.u.x, prms->m_bias.u.y, prms->m_bias.u.z);

	return AKRET_PROC_SUCCEED;
}

/*!
 Calculate Yaw, Pitch, Roll angle.
 m_hvec, m_avec and m_gvec should be Android coordination.
 @return Always return #AKRET_PROC_SUCCEED.
 @param[in,out] prms A pointer to a #AKSCPRMS structure.
 */
int16 CalcDirection(AKSCPRMS* prms)
{
	/* Conversion matrix from Android to SmartCompass coordination */
	int16 preThe, swp;
	const I16MATRIX hlayout = {{
								 0, 1, 0,
								-1, 0, 0,
								 0, 0, 1}};
	const I16MATRIX alayout = {{
								 0,-1, 0,
								 1, 0, 0,
								 0, 0,-1}};

	preThe = prms->m_theta;

	prms->m_d6dRet = AKSC_DirectionS3(
			prms->m_licenser,
			prms->m_licensee,
			prms->m_key,
			&prms->m_hvec,
			&prms->m_avec,
			&prms->m_dvec,
			&hlayout,
			&alayout,
			&prms->m_theta,
			&prms->m_delta,
			&prms->m_hr,
			&prms->m_hrhoriz,
			&prms->m_ar,
			&prms->m_phi180,
			&prms->m_phi90,
			&prms->m_eta180,
			&prms->m_eta90,
			&prms->m_mat,
			&prms->m_quat);

	prms->m_theta =	AKSC_ThetaFilter(
			prms->m_theta,
			preThe,
			THETAFILTER_SCALE);

	if (prms->m_d6dRet == AKSC_CERTIFICATION_DENIED) {
		AKMERROR;
		return AKRET_PROC_FAIL;
	}

	if (prms->m_d6dRet != 3) {
		AKMDEBUG(AKMDBG_LEVEL0,
				"AKSC_Direction6D failed (0x%02x).\n"
				"  hvec=%d,%d,%d  avec=%d,%d,%d  dvec=%d,%d,%d\n",
				prms->m_d6dRet,
				prms->m_hvec.u.x, prms->m_hvec.u.y, prms->m_hvec.u.z,
				prms->m_avec.u.x, prms->m_avec.u.y, prms->m_avec.u.z,
				prms->m_dvec.u.x, prms->m_dvec.u.y, prms->m_dvec.u.z);
	}

	/* Convert Yaw, Pitch, Roll angle to Android coordinate system */
	if (prms->m_d6dRet & 0x02) {
		/*
		 from: AKM coordinate, AKSC units
		 to  : Android coordinate, AKSC units. */
		prms->m_eta180 = -prms->m_eta180;
		prms->m_eta90  = -prms->m_eta90;
		/*
		 from: AKM coordinate, AKSC units
		 to  : Android coordinate, AKSC units. */
		swp = prms->m_quat.u.x;
		prms->m_quat.u.x = prms->m_quat.u.y;
		prms->m_quat.u.y = -(swp);
		prms->m_quat.u.z = -(prms->m_quat.u.z);

		AKMDEBUG(AKMDBG_LEVEL3, "AKSC_Direction6D (0x%02x):\n"
				"  Yaw, Pitch, Roll=%6.1f,%6.1f,%6.1f\n",
				prms->m_d6dRet,
				DISP_CONV_Q6F(prms->m_theta),
				DISP_CONV_Q6F(prms->m_phi180),
				DISP_CONV_Q6F(prms->m_eta90));
	}

	return AKRET_PROC_SUCCEED;
}

/*!
 Calculate angular speed.
 m_hvec and m_avec should be Android coordination.
 @return
 @param[in,out] prms A pointer to a #AKSCPRMS structure.
 */
int16 CalcAngularRate(AKSCPRMS* prms)
{
	/* Conversion matrix from Android to SmartCompass coordination */
	const I16MATRIX hlayout = {{
								 0, 1, 0,
								-1, 0, 0,
								 0, 0, 1}};
	const I16MATRIX alayout = {{
								 0,-1, 0,
								 1, 0, 0,
								 0, 0,-1}};


	int16vec tmp_hvec;
	int16 aksc_ret;
	int32		swp;

	// Subtract offset from non-averaged value.
	aksc_ret = AKSC_VNorm(
						  &prms->m_hdata[0],
						  &prms->m_ho,
						  &prms->m_hs,
						  AKSC_HSENSE_TARGET,
						  &tmp_hvec
						  );
	if (aksc_ret == 0) {
		AKMERROR;
		AKMDEBUG(AKMDBG_LEVEL3,"AKSC_VNorm failed.\n"
				"  have=%6d,%6d,%6d  ho=%6d,%6d,%6d  hs=%6d,%6d,%6d\n",
				prms->m_hdata[0].u.x, prms->m_hdata[0].u.y, prms->m_hdata[0].u.z,
				prms->m_ho.u.x, prms->m_ho.u.y, prms->m_ho.u.z,
				prms->m_hs.u.x, prms->m_hs.u.y, prms->m_hs.u.z);
		return AKRET_PROC_FAIL;
	}

#if 0
	// Convert to Android coordination
	ConvertCoordinate(prms->m_hlayout, &tmp_hvec);
#endif

	prms->m_pgRet = AKSC_PseudoGyro(
						&prms->m_pgcond,
						prms->m_pgdt,
						&tmp_hvec,
						&prms->m_avec,
						&hlayout,
						&alayout,
						&prms->m_pgvar,
						&prms->m_pgout,
						&prms->m_pgquat,
						&prms->m_pgGravity,
						&prms->m_pgLinAcc
					);

	if(prms->m_pgRet != 1) {
		AKMERROR;
		AKMDEBUG(AKMDBG_LEVEL3,"AKSC_PseudoGyro failed: dt=%6.2f\n"
				"  hvec=%8.2f,%8.2f,%8.2f  avec=%8.5f,%8.5f,%8.5f\n",
				prms->m_pgdt/16.0f,
				tmp_hvec.u.x/16.0f, tmp_hvec.u.y/16.0f, tmp_hvec.u.z/16.0f,
				prms->m_avec.u.x/720.0f, prms->m_avec.u.y/720.0f, prms->m_avec.u.z/720.0f);
		return AKRET_PROC_FAIL;
	} else {
		/* Convertion:
		 from: AKM coordinate
		 to  : Android coordinate
		 Unit conversion will be done in HAL. */
		swp = prms->m_pgout.u.x;
		prms->m_pgout.u.x = -(prms->m_pgout.u.y);
		prms->m_pgout.u.y = swp;
		prms->m_pgout.u.z = prms->m_pgout.u.z;

		swp = prms->m_pgquat.u.x;
		prms->m_pgquat.u.x = prms->m_pgquat.u.y;
		prms->m_pgquat.u.y = -(swp);
		prms->m_pgquat.u.z = -(prms->m_pgquat.u.z);

		swp = prms->m_pgGravity.u.x;
		prms->m_pgGravity.u.x = prms->m_pgGravity.u.y;
		prms->m_pgGravity.u.y = -(swp);
		prms->m_pgGravity.u.z = -(prms->m_pgGravity.u.z);

		swp = prms->m_pgLinAcc.u.x;
		prms->m_pgLinAcc.u.x = prms->m_pgLinAcc.u.y;
		prms->m_pgLinAcc.u.y = -(swp);
		prms->m_pgLinAcc.u.z = -(prms->m_pgLinAcc.u.z);

		AKMDEBUG(AKMDBG_LEVEL3, "AKSC_PseudoGyro:\n"
			"  dt=%6.2f rate=%8.2f,%8.2f,%8.2f quat=%8.5f,%8.5f,%8.5f,%8.5f\n",
			prms->m_pgdt/16.0f,
			prms->m_pgout.u.x/64.0f,
			prms->m_pgout.u.y/64.0f,
			prms->m_pgout.u.z/64.0f,
			prms->m_pgquat.u.x/16384.0f,
			prms->m_pgquat.u.y/16384.0f,
			prms->m_pgquat.u.z/16384.0f,
			prms->m_pgquat.u.w/16384.0f);
	}

	return AKRET_PROC_SUCCEED;
}


