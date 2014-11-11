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

/**********************************************************************************
   INCLUDE FILES
***********************************************************************************/

#include "VectorArithmetic.h"

#ifdef __ARM_HAVE_NEON
#include <arm_neon.h>
#endif

#define LOG_TAG "LVM"
#include <utils/Log.h>

/**********************************************************************************
   FUNCTION From2iToMono_32
***********************************************************************************/

void From2iToMono_32( const LVM_INT32 *src,
                            LVM_INT32 *dst,
                            LVM_INT16 n)
{
#if !(defined __ARM_HAVE_NEON)
  LVM_INT16 ii;
    LVM_INT32 Temp;

    for (ii = n; ii != 0; ii--)
    {
        Temp = (*src>>1);
        src++;

        Temp +=(*src>>1);
        src++;

        *dst = Temp;
        dst++;
    }
#else
    LVM_INT16 ii;
    int32x2_t src1, src2, d;
    if((n&1) != 0){
      ALOGE("n=%d, not 2 aligned", n);    
    }
    for(ii= 0; ii<n; ii+=2){
      src1 = vld1_s32(src); // l & r
      src += 2;
      src2 = vld1_s32(src); // l & r
      src += 2;
      d = vpadd_s32(src1, src2);
      vst1_s32(dst, d);     // save result
      dst += 2;
    }
#endif
    return;
  
}

/**********************************************************************************/
