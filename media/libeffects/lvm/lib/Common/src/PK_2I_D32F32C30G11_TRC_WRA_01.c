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
#include "PK_2I_D32F32CllGss_TRC_WRA_01_Private.h"
#include "LVM_Macros.h"

#ifdef __ARM_HAVE_NEON
#include <arm_neon.h>
#endif

#define LOG_TAG "LVM"
#include <utils/Log.h>
/**************************************************************************
 ASSUMPTIONS:
 COEFS-
 pBiquadState->coefs[0] is A0,
 pBiquadState->coefs[1] is -B2,
 pBiquadState->coefs[2] is -B1, these are in Q30 format
 pBiquadState->coefs[3] is Gain, in Q11 format


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
void PK_2I_D32F32C30G11_TRC_WRA_01 ( Biquad_Instance_t       *pInstance,
                                     LVM_INT32               *pDataIn,
                                     LVM_INT32               *pDataOut,
                                     LVM_INT16               NrSamples)
    {
#if !(defined  __ARM_HAVE_NEON)
      LVM_INT32 ynL,ynR,ynLO,ynRO,templ;
        LVM_INT16 ii;
        PFilter_State pBiquadState = (PFilter_State) pInstance;
         
        for (ii = NrSamples; ii != 0; ii--)
         {


            /**************************************************************************
                            PROCESSING OF THE LEFT CHANNEL
            ***************************************************************************/
            /* ynL= (A0 (Q30) * (x(n)L (Q0) - x(n-2)L (Q0) ) >>30)  in Q0*/
            templ=(*pDataIn)-pBiquadState->pDelays[2];
            MUL32x32INTO32(templ,pBiquadState->coefs[0],ynL,30)

            /* ynL+= ((-B2 (Q30) * y(n-2)L (Q0) ) >>30) in Q0*/
            MUL32x32INTO32(pBiquadState->pDelays[6],pBiquadState->coefs[1],templ,30)
            ynL+=templ;

            /* ynL+= ((-B1 (Q30) * y(n-1)L (Q0) ) >>30) in Q0 */
            MUL32x32INTO32(pBiquadState->pDelays[4],pBiquadState->coefs[2],templ,30)
            ynL+=templ;

            /* ynLO= ((Gain (Q11) * ynL (Q0))>>11) in Q0*/
            MUL32x16INTO32(ynL,pBiquadState->coefs[3],ynLO,11)
            /* ynLO=( ynLO(Q0) + x(n)L (Q0) ) in Q0*/
            ynLO+= (*pDataIn);

            /**************************************************************************
                            PROCESSING OF THE RIGHT CHANNEL
            ***************************************************************************/
            /* ynR= (A0 (Q30) * (x(n)R (Q0) - x(n-2)R (Q0) ) >>30)   in Q0*/
            templ=(*(pDataIn+1))-pBiquadState->pDelays[3];
            MUL32x32INTO32(templ,pBiquadState->coefs[0],ynR,30)

            /* ynR+= ((-B2 (Q30) * y(n-2)R (Q0) ) >>30)  in Q0*/
            MUL32x32INTO32(pBiquadState->pDelays[7],pBiquadState->coefs[1],templ,30)
            ynR+=templ;

            /* ynR+= ((-B1 (Q30) * y(n-1)R (Q0) ) >>30)  in Q0 */
            MUL32x32INTO32(pBiquadState->pDelays[5],pBiquadState->coefs[2],templ,30)
            ynR+=templ;

            /* ynRO= ((Gain (Q11) * ynR (Q0))>>11) in Q0*/
            MUL32x16INTO32(ynR,pBiquadState->coefs[3],ynRO,11)

            /* ynRO=( ynRO(Q0) + x(n)R (Q0) ) in Q0*/
            ynRO+= (*(pDataIn+1));

            /**************************************************************************
                            UPDATING THE DELAYS
            ***************************************************************************/
            pBiquadState->pDelays[7]=pBiquadState->pDelays[5]; /* y(n-2)R=y(n-1)R*/
            pBiquadState->pDelays[6]=pBiquadState->pDelays[4]; /* y(n-2)L=y(n-1)L*/
            pBiquadState->pDelays[3]=pBiquadState->pDelays[1]; /* x(n-2)R=x(n-1)R*/
            pBiquadState->pDelays[2]=pBiquadState->pDelays[0]; /* x(n-2)L=x(n-1)L*/
            pBiquadState->pDelays[5]=ynR; /* Update y(n-1)R in Q0*/
            pBiquadState->pDelays[4]=ynL; /* Update y(n-1)L in Q0*/
            pBiquadState->pDelays[0]=(*pDataIn); /* Update x(n-1)L in Q0*/
            pDataIn++;
            pBiquadState->pDelays[1]=(*pDataIn); /* Update x(n-1)R in Q0*/
            pDataIn++;

            /**************************************************************************
                            WRITING THE OUTPUT
            ***************************************************************************/
            *pDataOut=ynLO; /* Write Left output in Q0*/
            pDataOut++;
            *pDataOut=ynRO; /* Write Right ouput in Q0*/
            pDataOut++;
        }
#else
        LVM_INT16 ii =0;
        PFilter_State pBiquadState = (PFilter_State) pInstance;
        LVM_INT32* coefs = pBiquadState->coefs;
        LVM_INT32* delays = pBiquadState->pDelays;
        LVM_INT32 tmp;

        LVM_INT32 A0 = pBiquadState->coefs[0];
        LVM_INT32 B2 = pBiquadState->coefs[1];
        LVM_INT32 B1 = pBiquadState->coefs[2];
        LVM_INT32 G  = pBiquadState->coefs[3];
        
        int32x2_t X_2 = vld1_s32(&pBiquadState->pDelays[2]); // X_2L, X_2R
        int32x2_t Y_2 = vld1_s32(&pBiquadState->pDelays[6]); // Y_2L, Y_2R
        int32x2_t Y_1 = vld1_s32(&pBiquadState->pDelays[4]); // Y_1L, Y_1R
        int32x2_t X_1 = vld1_s32(&pBiquadState->pDelays[0]); // X_1L, X_1R
        
        asm volatile(
        "  vdup.32 d0, %[A0]                        \n\t"   // A0
        "  vdup.32 d1, %[B2]                        \n\t"   // B2                        
        "  vdup.32 d2, %[B1]                        \n\t"   // B1
        "  vdup.32 d3, %[G]                         \n\t"   // G
        "                                           \n\t"
        "  vld1.32 d4, [%[delays]]!                 \n\t"    //  X_1
        "  vld1.32 d5, [%[delays]]!                 \n\t"    //  X_2
        "  vld1.32 d6, [%[delays]]!                 \n\t"    //  Y_1
        "  vld1.32 d7, [%[delays]]!                 \n\t"    // Y_2
        "loop:                                         \n\t"
        "  vld1.32 d8, [%[pDataIn]]!                \n\t"
        "                                           \n\t"
        "  vmull.s32 q5, d0, d8                     \n\t" // ; A0*X
        "  vmlsl.s32 q5, d0, d5                     \n\t"    // ; -A0*X_2
        "  vmlal.s32 q5, d1, d7                     \n\t"   // ; B2*Y_2
        "  vmlal.s32 q5, d2, d6                     \n\t"   // ; B1*Y_1
        "                                           \n\t"
        "  vmov.s32 d7, #0                           \n\t"
        "  vadd.s32 d7, d7, d6                       \n\t"   // ; Y_2 = Y_1 
        "  vmov.s32 d5, #0                           \n\t"
        "  vadd.s32 d5, d5, d4                       \n\t"  // ; X_2 = X_1
        "                                           \n\t"
        "  vshrn.i32 d9, q5, #15                     \n\t"  // ; Y >>=  30
        "  vshr.s32 d9, d9, #15                     \n\t"
        "                                           \n\t"
        "  vmov.s32 d4, #0                           \n\t"
        "  vadd.s32 d4, d4, d8                       \n\t"   // ; X_1 = *pDataIn
        "                                           \n\t"
        "  vmull.s32 q5, d9, d3                     \n\t"    // ; Y= Y*G 
        "  vshrn.s32 d9, q5, #11                     \n\t"     // ; Y >>= 11
        "                                           \n\t"
        "  vadd.s32 d9, d9, d8                       \n\t"  // ; *pDataIn + Y 
        "  vmov.s32 d6, #0                           \n\t"
        "  vadd.s32 d6, d6, d9                       \n\t" // ; Y_1 = Y 
        "                                           \n\t"
        "  vst1.32 d9, [%[pDataOut]]!               \n\t"
        "  add %[ii], %[ii], #1                      \n\t"
        "  cmp %[ii], %[NrSamples]                  \n\t"
        "  blt   loop                                 \n\t"
        "                                           \n\t"
        "  sub   %[delays], %[delays], #32          \n\t"
        "  vst1.32 d4, [%[delays]]!                 \n\t"
        "  vst1.32 d5, [%[delays]]!                 \n\t"
        "  vst1.32 d6, [%[delays]]!                 \n\t"
        "  vst1.32 d7, [%[delays]]!                 \n\t"
        :[pDataOut]"+r"(pDataOut)
        :[pDataIn]"r"(pDataIn),[A0]"r"(A0),[B2]"r"(B2),[B1]"r"(B1),[G]"r"(G),[ii]"r"(ii),[NrSamples]"r"(NrSamples),[delays]"r"(delays)
        :"d0","d1","d2","d3","d4","d5","d6","d7","d8","d9","d10","d11"
        );                      

#endif
    }

