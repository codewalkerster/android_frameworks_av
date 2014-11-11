
//#define LOG_NDEBUG 0
#define LOG_TAG "AMLVENC"
#include <utils/Log.h>

#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

//#include "avcint_common.h"
//#include "avcapi_common.h"
#include "avcenc_api.h"
#include "avclib_common.h"

#include "amvenclib.h"
#include "AML_HWEncoder.h"

#define AMVENC_NAME "AMLVENC"

extern AMVEnc_Status AMRCUpdateFrame(amvenc_drv_t *p, AMVencRateControl *rateCtrl, bool IDR);
extern void AMRCInitFrameQP(amvenc_drv_t *p, AMVencRateControl *rateCtrl);
extern void AMRCUpdateBuffer(AMVencRateControl *rateCtrl, int frameInc);
extern void AMCleanupRateControlModule(AMVencRateControl *rateCtrl);
extern AMVEnc_Status AMInitRateControlModule(AMVencRateControl *rateCtrl);

AVCEnc_Status AMVEncRCDetermineFrameNum(amvenc_info_t* info, uint32 modTime, uint *frameNum)
{
    AMVencRateControl* rc = &info->rc;
    amvenc_drv_t* p = (amvenc_drv_t*)info->privdata;
    uint32 modTimeRef = info->modTimeRef;
    int32  currFrameNum ;
    int  frameInc;

    /* check with the buffer fullness to make sure that we have enough bits to encode this frame */
    /* we can use a threshold to guarantee minimum picture quality */
    /**********************************/

    /* for now, the default is to encode every frame, To Be Changed */
    if (rc->first_frame){
        info->modTimeRef = modTime;
        info->wrapModTime = 0;
        info->prevFrameNum = 0;
        info->prevProcFrameNum = 0;

        *frameNum = 0;

        /* set frame type to IDR-frame */
        info->nal_unit_type = AVC_NALTYPE_IDR;
        info->slice_type = AVC_I_SLICE;
        return AVCENC_SUCCESS;
    }else{
        if (modTime < modTimeRef){ 
            /* modTime wrapped around */
            info->wrapModTime += ((uint32)0xFFFFFFFF - modTimeRef) + 1;
            info->modTimeRef = modTimeRef = 0;
        }
        modTime += info->wrapModTime; /* wrapModTime is non zero after wrap-around */

        currFrameNum = (int32)(((modTime - modTimeRef) * rc->frame_rate + 200) / 1000); /* add small roundings */

        if (currFrameNum <= (int32)info->prevProcFrameNum)
            return AVCENC_FAIL;  /* this is a late frame do not encode it */

        frameInc = currFrameNum - info->prevProcFrameNum;

        if (frameInc < rc->skip_next_frame + 1){
            return AVCENC_FAIL;  /* frame skip required to maintain the target bit rate. */
        }

        AMRCUpdateBuffer(rc, frameInc);

        *frameNum = currFrameNum;

        /* This part would be similar to DetermineVopType of m4venc */
        if ((*frameNum >= (uint)rc->idrPeriod && rc->idrPeriod > 0) || (*frameNum > info->MaxFrameNum)){ /* first frame or IDR*/
            /* set frame type to IDR-frame */
            if (rc->idrPeriod){
                info->modTimeRef += (uint32)(rc->idrPeriod * 1000 / rc->frame_rate);
                *frameNum -= rc->idrPeriod;
            }else{
                info->modTimeRef += (uint32)(info->MaxFrameNum * 1000 / rc->frame_rate);
                *frameNum -= info->MaxFrameNum;
            }

            info->nal_unit_type = AVC_NALTYPE_IDR;
            info->slice_type = AVC_I_SLICE;
            info->prevProcFrameNum = *frameNum;
        }else{
            info->nal_unit_type = AVC_NALTYPE_SLICE;
            info->slice_type = AVC_P_SLICE;
            info->prevProcFrameNum = currFrameNum;
        }
    }
    ALOGV("Get Nal Type: %s", (info->nal_unit_type ==AVC_NALTYPE_IDR)?"IDR":"SLICE");
    return AVCENC_SUCCESS;
}

int AML_HWEncInitialize(void *Handle, void *encParam)
{
    AVCEnc_Status status = AVCENC_FAIL;
    AVCHandle *avcHandle = (AVCHandle *)Handle;
    AVCEncParams *avcencParam = (AVCEncParams *)encParam;
    amvenc_info_t* info = (amvenc_info_t*)calloc(1,sizeof(amvenc_info_t));
    amvenc_drv_t* p = NULL;
    int ii, maxFrameNum;

    if(!info){
        status = AVCENC_MEMORY_FAIL;
        goto exit;
    }

    if (avcencParam->frame_rate == 0){
        status =  AVCENC_INVALID_FRAMERATE;
        goto exit;
    }
    memset(info,0,sizeof(amvenc_info_t));

    info->enc_width = avcencParam->width;
    info->enc_height = avcencParam->height;
    info->outOfBandParamSet = avcencParam->out_of_band_param_set;
    info->fullsearch_enable = avcencParam->fullsearch;

    maxFrameNum = (avcencParam->idr_period == -1) ? (1 << 16) : avcencParam->idr_period;
    ii = 0;
    while (maxFrameNum > 0){
        ii++;
        maxFrameNum >>= 1;
    }
    if (ii < 4) ii = 4;
    else if (ii > 16) ii = 16;

    info->MaxFrameNum = 1 << ii; //(LOG2_MAX_FRAME_NUM_MINUS4 + 4); /* default */

    /* now the rate control and performance related parameters */
    //info->rc.scdEnable = (avcencParam->auto_scd == AVC_ON) ? TRUE : FALSE;
    info->rc.idrPeriod = avcencParam->idr_period + 1;
    //info->rc.intraMBRate = avcencParam->intramb_refresh;
    //info->rc.dpEnable = (avcencParam->data_par == AVC_ON) ? TRUE : FALSE;

    //info->rc.subPelEnable = (avcencParam->sub_pel == AVC_ON) ? TRUE : FALSE;
    info->rc.mvRange = avcencParam->search_range;

    //info->rc.subMBEnable = (avcencParam->submb_pred == AVC_ON) ? TRUE : FALSE;
    //info->rc.rdOptEnable = (avcencParam->rdopt_mode == AVC_ON) ? TRUE : FALSE;
    //info->rc.bidirPred = (avcencParam->bidir_pred == AVC_ON) ? TRUE : FALSE;

    info->rc.rcEnable = (avcencParam->rate_control == AVC_ON) ? TRUE : FALSE;
    info->rc.bitRate = avcencParam->bitrate;
    info->rc.cpbSize = avcencParam->CPB_size;
    //info->rc.initDelayOffset = (info->rc.bitRate * avcencParam->init_CBP_removal_delay / 1000);
    info->rc.frame_rate = (float)(avcencParam->frame_rate * 1.0 / 1000);

    info->rc.initQP = avcencParam->initQP;
    if(info->rc.initQP == 0){
        double L1, L2, L3, bpp;
        bpp = 1.0 * info->rc.bitRate /(info->rc.frame_rate * (info->enc_width *info->enc_height));
        if (info->enc_width <= 176){
            L1 = 0.1;
            L2 = 0.3;
            L3 = 0.6;
        }else if (info->enc_width <= 352){
            L1 = 0.2;
            L2 = 0.6;
            L3 = 1.2;
        }else{
            L1 = 0.6;
            L2 = 1.4;
            L3 = 2.4;
        }

        if (bpp <= L1)
            info->rc.initQP = 35;
        else if (bpp <= L2)
            info->rc.initQP = 25;
        else if (bpp <= L3)
            info->rc.initQP = 20;
        else
            info->rc.initQP = 15;
    }

    if(AMInitRateControlModule(&info->rc)!=AMVENC_SUCCESS){
        status = AVCENC_MEMORY_FAIL;
        goto exit;
    }
    info->rc.first_frame = 1; /* set this flag for the first time */

    info->modTimeRef = 0;     /* ALWAYS ASSUME THAT TIMESTAMP START FROM 0 !!!*/
    info->prevFrameNum = 0;
    info->prevCodedFrameNum = 0;

    p = InitAMVEncode(info->enc_width, info->enc_height, info->rc.initQP,info->rc.mvRange,info->fullsearch_enable);
    if(!p){
        status = AVCENC_FAIL;
        goto exit;
    }
    if (info->outOfBandParamSet == TRUE)
        info->state= PlatFormEnc_Encoding_SPS;
    else
        info->state = PlatFormEnc_Analyzing_Frame;
    info->privdata = (void*)p;
    avcHandle->platform_enc->privdata = (void *)info;
    return (int)AVCENC_SUCCESS;
exit:
    if(info){
        AMCleanupRateControlModule(&info->rc);
        UnInitAMVEncode(p);
        free(info);
    }
    ALOGE("AML_HWEncInitialize Fail, error=%d",status);
    return (int)status;
}

int AML_HWSetInput(void *Handle, void *input)
{
    uint frameNum;
    AVCEnc_Status ret = AVCENC_FAIL;
    AVCHandle *avcHandle = (AVCHandle *)Handle;
    AVCFrameIO *avcInput = (AVCFrameIO *)input;
    AMVEnc_Status status = AMVENC_FAIL;
    amvenc_info_t* info = (amvenc_info_t*)avcHandle->platform_enc->privdata;
    amvenc_drv_t* p = NULL;
    unsigned yuv[3];

    if (info == NULL){
        ALOGE("AML_HWSetInput Fail: UNINITIALIZED");
        return (int)AVCENC_UNINITIALIZED;
    }
    p = (amvenc_drv_t*)info->privdata;

    if (info->state== PlatFormEnc_WaitingForBuffer){
        goto RECALL_INITFRAME;
    }else if (info->state != PlatFormEnc_Analyzing_Frame){
        ALOGE("AML_HWSetInput Wrong state: %d",info->state);
        return (int)AVCENC_FAIL;
    }
    if (avcInput->pitch > 0xFFFF){
        ALOGE("AML_HWSetInput Fail: NOT_SUPPORTED");
        return (int)AVCENC_NOT_SUPPORTED; // we use 2-bytes for pitch
    }
    if (AVCENC_SUCCESS != AMVEncRCDetermineFrameNum(info, avcInput->coding_timestamp, &frameNum)){
        ALOGD("AML_HWSetInput SKIPPED_PICTURE");
        return (int)AVCENC_SKIPPED_PICTURE; /* not time to encode, thus skipping */
    }

    yuv[0]= (unsigned)avcInput->YCbCr[0];
    yuv[1]= (unsigned)avcInput->YCbCr[1];
    yuv[2]= (unsigned)avcInput->YCbCr[2];
    info->coding_order = frameNum;

RECALL_INITFRAME:
    /* initialize and analyze the frame */
    status = AMVEncodeInitFrame(p, &yuv[0], (info->nal_unit_type == AVC_NALTYPE_IDR));

    if((status == AMVENC_NEW_IDR)&&(info->nal_unit_type != AVC_NALTYPE_IDR)){
        info->modTimeRef = avcInput->coding_timestamp;
        info->coding_order = 0;
        info->nal_unit_type = AVC_NALTYPE_IDR;
        info->slice_type = AVC_I_SLICE;
        info->prevProcFrameNum = 0;
    }

    AMRCInitFrameQP(p,&info->rc);

    if (status == AMVENC_SUCCESS){
        info->state = PlatFormEnc_Encoding_Frame;
        ret = AVCENC_SUCCESS;
    }else if (status == AMVENC_NEW_IDR){
        if (info->outOfBandParamSet == TRUE)
            info->state = PlatFormEnc_Encoding_Frame;
        else // assuming that in-band paramset keeps sending new SPS and PPS.
            info->state = PlatFormEnc_Encoding_SPS;
        ret = AVCENC_NEW_IDR;
    }else if (status == AMVENC_PICTURE_READY){
        info->state = PlatFormEnc_WaitingForBuffer; // Input accepted but can't continue
        ret = AVCENC_PICTURE_READY;
    }
    ALOGV("AML_HWSetInput status: %d",ret);
    return (int)ret;
}

int AML_HWEncNAL(void *Handle, unsigned char *buffer, unsigned int *buf_nal_size, int *nal_type)
{
    AVCEnc_Status ret = AVCENC_FAIL;
    AVCHandle *avcHandle = (AVCHandle *)Handle;
    AMVEnc_Status status = AMVENC_FAIL;
    amvenc_info_t* info = (amvenc_info_t*)avcHandle->platform_enc->privdata;
    amvenc_drv_t* p = NULL;
    int datalen = 0;

    if (info == NULL){
        ALOGE("AML_HWEncNAL Fail: UNINITIALIZED");
        return (int)AVCENC_UNINITIALIZED;
    }

    p = (amvenc_drv_t*)info->privdata;
    ALOGV("AML_HWEncNAL status: %d",info->state);
    switch (info->state){
        case PlatFormEnc_Initializing:
            return (int)AVCENC_UNINITIALIZED;
        case PlatFormEnc_Encoding_SPS:
            /* encode SPS */
            status = AMVEncodeSPS_PPS(p, buffer,&datalen);
            if(status!=AMVENC_SUCCESS){
                ALOGV("AML_HWEncNAL status: %d, err=%d",info->state,status);
                return (int)ret;
            }
            ret = AVCENC_WRONG_STATE;
            if (info->outOfBandParamSet == TRUE) 
                info->state = PlatFormEnc_Analyzing_Frame;
            else    // SetInput has been called before SPS and PPS.
                info->state = PlatFormEnc_Encoding_Frame;

            *nal_type = AVC_NALTYPE_PPS;
            *buf_nal_size = datalen;
            break;
        case PlatFormEnc_Encoding_Frame:
            status = AMVEncodeSlice(p,buffer,&datalen);
            if ((status != AMVENC_PICTURE_READY)&&(status != AMVENC_SUCCESS)){
                ALOGV("AML_HWEncNAL status: %d, err=%d",info->state,status);
                return (int)ret;
            }
            *nal_type = info->nal_unit_type;
            *buf_nal_size = datalen;
            info->rc.numFrameBits = datalen<<3;

            if (status == AMVENC_PICTURE_READY){
                status = AMRCUpdateFrame(p,&info->rc,(info->nal_unit_type == AVC_NALTYPE_IDR));
                if (status == AMVENC_SKIPPED_PICTURE){
                    info->state = PlatFormEnc_Analyzing_Frame;
                    ret = AVCENC_SKIPPED_PICTURE;
                    return (int)ret;
                }
                info->prevCodedFrameNum = info->coding_order;
                info->state = PlatFormEnc_Analyzing_Frame;
                ret = AVCENC_PICTURE_READY;
            }else{
                ret = AVCENC_SUCCESS;
            }
            break;
        default:
            ret = AVCENC_WRONG_STATE;
    }
    ALOGV("AML_HWEncNAL next status: %d, ret=%d",info->state, ret);
    return (int)ret;
}

int AML_HWEncRelease(void *Handle)
{
    AVCHandle *avcHandle = (AVCHandle *)Handle;
    amvenc_info_t* info = (amvenc_info_t*)avcHandle->platform_enc->privdata;
    if(info){
        AMCleanupRateControlModule(&info->rc);
        if(info->privdata)
            UnInitAMVEncode((amvenc_drv_t*)info->privdata);
        free(info);
    }
    ALOGV("AML_HWEncRelease Done");
    return (int)AVCENC_SUCCESS;
}

extern "C" void GetPlatformEncHandle(PlatformEnc_t* enc)
{
    ALOGV("AML_HWEnc Get Handle Enter");
    if((enc)&&(enc->func)){
        enc->name = AMVENC_NAME;
        enc->opened = 0;
        enc->privdata = NULL;
        enc->func->Initialize = AML_HWEncInitialize;
        enc->func->SetInput = AML_HWSetInput;
        enc->func->EncodeNAL = AML_HWEncNAL;
        enc->func->Release = AML_HWEncRelease;
        enc->available = true;
        ALOGV("AML_HWEnc Get Handle Done");
    }
    return;
}

