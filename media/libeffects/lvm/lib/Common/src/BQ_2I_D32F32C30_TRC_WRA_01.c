/*
 * Copyright (C) 2004-2010 NXP Software
 * Copyright (C) 2010 The Android Open Source Project
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

#include "BIQUAD.h"
#include "BQ_2I_D32F32Cll_TRC_WRA_01_Private.h"
#include "LVM_Macros.h"

#ifdef __ARM_HAVE_NEON
#include <arm_neon.h>
#endif

/**************************************************************************
 ASSUMPTIONS:
 COEFS-
 pBiquadState->coefs[0] is A2, pBiquadState->coefs[1] is A1
 pBiquadState->coefs[2] is A0, pBiquadState->coefs[3] is -B2
 pBiquadState->coefs[4] is -B1, these are in Q30 format

 DELAYS-
 pBiquadState->pDelays[0] is x(n-1)L in Q0 format
 pBiquadState->pDelays[1] is x(n-1)R in Q0 format
 pBiquadState->pDelays[2] is x(n-2)L in Q0 format
 pBiquadState->pDelays[3] is x(n-2)R in Q0 format
 pBiquadState->pDelays[4] is y(n-1)L in Q0 format
 pBiquadState->pDelays[5] is y(n-1)R in Q0 format
 pBiquadState->pDelays[6] is y(n-2)L in Q0 format
 pBiquadState->pDelays[7] is y(n-2)R in Q0 format
***************************************************************************/

void BQ_2I_D32F32C30_TRC_WRA_01 (           Biquad_Instance_t       *pInstance,
                                            LVM_INT32                    *pDataIn,
                                            LVM_INT32                    *pDataOut,
                                            LVM_INT16                    NrSamples)


    {
#if !(defined  __ARM_HAVE_NEON)
        LVM_INT32 ynL,ynR,templ,tempd;
        LVM_INT16 ii;
        PFilter_State pBiquadState = (PFilter_State) pInstance;

         for (ii = NrSamples; ii != 0; ii--)
         {


            /**************************************************************************
                            PROCESSING OF THE LEFT CHANNEL
            ***************************************************************************/
            /* ynL= ( A2 (Q30) * x(n-2)L (Q0) ) >>30 in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[0],pBiquadState->pDelays[2],ynL,30)

            /* ynL+= ( A1 (Q30) * x(n-1)L (Q0) ) >> 30 in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[1],pBiquadState->pDelays[0],templ,30)
            ynL+=templ;

            /* ynL+= ( A0 (Q30) * x(n)L (Q0) ) >> 30 in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[2],*pDataIn,templ,30)
            ynL+=templ;

             /* ynL+= (-B2 (Q30) * y(n-2)L (Q0) ) >> 30 in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[3],pBiquadState->pDelays[6],templ,30)
            ynL+=templ;

            /* ynL+= (-B1 (Q30) * y(n-1)L (Q0) ) >> 30 in Q0 */
            MUL32x32INTO32(pBiquadState->coefs[4],pBiquadState->pDelays[4],templ,30)
            ynL+=templ;

            /**************************************************************************
                            PROCESSING OF THE RIGHT CHANNEL
            ***************************************************************************/
            /* ynR= ( A2 (Q30) * x(n-2)R (Q0) ) >> 30 in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[0],pBiquadState->pDelays[3],ynR,30)

            /* ynR+= ( A1 (Q30) * x(n-1)R (Q0) ) >> 30  in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[1],pBiquadState->pDelays[1],templ,30)
            ynR+=templ;

            /* ynR+= ( A0 (Q30) * x(n)R (Q0) ) >> 30 in Q0*/
            tempd=*(pDataIn+1);
            MUL32x32INTO32(pBiquadState->coefs[2],tempd,templ,30)
            ynR+=templ;

            /* ynR+= (-B2 (Q30) * y(n-2)R (Q0) ) >> 30 in Q0*/
            MUL32x32INTO32(pBiquadState->coefs[3],pBiquadState->pDelays[7],templ,30)
            ynR+=templ;

            /* ynR+= (-B1 (Q30) * y(n-1)R (Q0) ) >> 30 in Q0 */
            MUL32x32INTO32(pBiquadState->coefs[4],pBiquadState->pDelays[5],templ,30)
            ynR+=templ;

            /**************************************************************************
                            UPDATING THE DELAYS
            ***************************************************************************/
            pBiquadState->pDelays[7]=pBiquadState->pDelays[5]; /* y(n-2)R=y(n-1)R*/
            pBiquadState->pDelays[6]=pBiquadState->pDelays[4]; /* y(n-2)L=y(n-1)L*/
            pBiquadState->pDelays[3]=pBiquadState->pDelays[1]; /* x(n-2)R=x(n-1)R*/
            pBiquadState->pDelays[2]=pBiquadState->pDelays[0]; /* x(n-2)L=x(n-1)L*/
            pBiquadState->pDelays[5]=(LVM_INT32)ynR; /* Update y(n-1)R in Q0*/
            pBiquadState->pDelays[4]=(LVM_INT32)ynL; /* Update y(n-1)L in Q0*/
            pBiquadState->pDelays[0]=(*pDataIn); /* Update x(n-1)L in Q0*/
            pDataIn++;
            pBiquadState->pDelays[1]=(*pDataIn); /* Update x(n-1)R in Q0*/
            pDataIn++;

            /**************************************************************************
                            WRITING THE OUTPUT
            ***************************************************************************/
            *pDataOut=(LVM_INT32)ynL; /* Write Left output in Q0*/
            pDataOut++;
            *pDataOut=(LVM_INT32)ynR; /* Write Right ouput in Q0*/
            pDataOut++;


        }
#else
        LVM_INT16 ii=0;
	      
		PFilter_State pBiquadState = (PFilter_State) pInstance;

		int32x2_t A2 = vdup_n_s32(pBiquadState->coefs[0]);
		int32x2_t A1 = vdup_n_s32(pBiquadState->coefs[1]);
		int32x2_t A0 = vdup_n_s32(pBiquadState->coefs[2]);
		int32x2_t B2 = vdup_n_s32(pBiquadState->coefs[3]);
		int32x2_t B1 = vdup_n_s32(pBiquadState->coefs[4]);
		
		int32x2_t X_2 = vld1_s32(&pBiquadState->pDelays[2]);
		int32x2_t X_1 = vld1_s32(&pBiquadState->pDelays[0]);
		int32x2_t Y_2 = vld1_s32(&pBiquadState->pDelays[6]);
		int32x2_t Y_1 = vld1_s32(&pBiquadState->pDelays[4]);

		for(ii=0; ii<NrSamples; ii++){
		  int32x2_t s = vld1_s32(pDataIn);
		  int64x2_t r = vmull_s32(A2, X_2);
		  r = vmlal_s32(r, A1, X_1);
		  r = vmlal_s32(r, A0, s);
		  r = vmlal_s32(r, B2, Y_2);
		  r = vmlal_s32(r, B1, Y_1);
		  int32_t ll =(int32_t)( vgetq_lane_s64(r, 0) >> 30);
		  int32_t rr =(int32_t)( vgetq_lane_s64(r, 1) >> 30);
		  pDataIn += 2;
		  *pDataOut ++ = ll;
		  *pDataOut ++ = rr;
		  int32_t tmp1, tmp2;
		  tmp1 = vget_lane_s32(X_1, 0);
		  tmp2 = vget_lane_s32(X_1, 1);
		  vset_lane_s32(tmp1, X_2, 0);
		  vset_lane_s32(tmp2, X_2, 1);
		  tmp1 = vget_lane_s32(Y_1, 0);
		  tmp2 = vget_lane_s32(Y_1, 1);
		  vset_lane_s32(tmp1, Y_2, 0);
		  vset_lane_s32(tmp2, Y_2, 1);

		  vset_lane_s32(ll, Y_1, 0);
		  vset_lane_s32(rr, Y_1, 1);
		  
		  tmp1 = vget_lane_s32(s, 0);
		  tmp2 = vget_lane_s32(s, 1);
		  vset_lane_s32(tmp1, X_1, 0);
		  vset_lane_s32(tmp2, X_1, 1);
		}
        vst1_s32(&pBiquadState->pDelays[2], X_2);
        vst1_s32(&pBiquadState->pDelays[0], X_1);
        vst1_s32(&pBiquadState->pDelays[6], Y_2);
        vst1_s32(&pBiquadState->pDelays[4], Y_1);
#endif         

    }

