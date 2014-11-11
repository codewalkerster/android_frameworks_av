#ifndef AML_VIDEO_ENCODER_H
#define AML_VIDEO_ENCODER_H

#include <utils/threads.h>
#include <semaphore.h>  

#define AVC_ABS(x)   (((x)<0)? -(x) : (x))
#define AVC_MAX(x,y) ((x)>(y)? (x):(y))
#define AVC_MIN(x,y) ((x)<(y)? (x):(y))
#define AVC_MEDIAN(A,B,C) ((A) > (B) ? ((A) < (C) ? (A) : (B) > (C) ? (B) : (C)): (B) < (C) ? (B) : (C) > (A) ? (C) : (A))

#define AMVENC_AVC_IOC_MAGIC  'E'

#define AMVENC_AVC_IOC_GET_ADDR			_IOW(AMVENC_AVC_IOC_MAGIC, 0x00, unsigned int)
#define AMVENC_AVC_IOC_INPUT_UPDATE		_IOW(AMVENC_AVC_IOC_MAGIC, 0x01, unsigned int)
#define AMVENC_AVC_IOC_GET_STATUS		_IOW(AMVENC_AVC_IOC_MAGIC, 0x02, unsigned int)
#define AMVENC_AVC_IOC_NEW_CMD			_IOW(AMVENC_AVC_IOC_MAGIC, 0x03, unsigned int)
#define AMVENC_AVC_IOC_GET_STAGE		_IOW(AMVENC_AVC_IOC_MAGIC, 0x04, unsigned int)
#define AMVENC_AVC_IOC_GET_OUTPUT_SIZE	_IOW(AMVENC_AVC_IOC_MAGIC, 0x05, unsigned int)
#define AMVENC_AVC_IOC_SET_QUANT 		_IOW(AMVENC_AVC_IOC_MAGIC, 0x06, unsigned int)
#define AMVENC_AVC_IOC_SET_ENCODER_WIDTH 		_IOW(AMVENC_AVC_IOC_MAGIC, 0x07, unsigned int)
#define AMVENC_AVC_IOC_SET_ENCODER_HEIGHT 		_IOW(AMVENC_AVC_IOC_MAGIC, 0x08, unsigned int)
#define AMVENC_AVC_IOC_CONFIG_INIT 				_IOW(AMVENC_AVC_IOC_MAGIC, 0x09, unsigned int)
#define AMVENC_AVC_IOC_FLUSH_CACHE 				_IOW(AMVENC_AVC_IOC_MAGIC, 0x0a, unsigned int)
#define AMVENC_AVC_IOC_FLUSH_DMA 				_IOW(AMVENC_AVC_IOC_MAGIC, 0x0b, unsigned int)
#define AMVENC_AVC_IOC_GET_BUFFINFO 				_IOW(AMVENC_AVC_IOC_MAGIC, 0x0c, unsigned int)

//---------------------------------------------------
// ENCODER_STATUS define
//---------------------------------------------------
#define ENCODER_IDLE              0
#define ENCODER_SEQUENCE          1
#define ENCODER_PICTURE           2
#define ENCODER_IDR               3
#define ENCODER_NON_IDR           4
#define ENCODER_MB_HEADER         5
#define ENCODER_MB_DATA           6

#define ENCODER_SEQUENCE_DONE          7
#define ENCODER_PICTURE_DONE           8
#define ENCODER_IDR_DONE               9
#define ENCODER_NON_IDR_DONE           10
#define ENCODER_MB_HEADER_DONE         11
#define ENCODER_MB_DATA_DONE           12


/* defines for H.264 IntraPredMode */
// 4x4 intra prediction modes 
#define HENC_VERT_PRED              0
#define HENC_HOR_PRED               1
#define HENC_DC_PRED                2
#define HENC_DIAG_DOWN_LEFT_PRED    3
#define HENC_DIAG_DOWN_RIGHT_PRED   4
#define HENC_VERT_RIGHT_PRED        5
#define HENC_HOR_DOWN_PRED          6
#define HENC_VERT_LEFT_PRED         7
#define HENC_HOR_UP_PRED            8

// 16x16 intra prediction modes
#define HENC_VERT_PRED_16   0
#define HENC_HOR_PRED_16    1
#define HENC_DC_PRED_16     2
#define HENC_PLANE_16       3

// 8x8 chroma intra prediction modes
#define HENC_DC_PRED_8     0
#define HENC_HOR_PRED_8    1
#define HENC_VERT_PRED_8   2
#define HENC_PLANE_8       3

/********************************************
* defines for H.264 mb_type 
********************************************/
#define HENC_MB_Type_PBSKIP                      0x0
#define HENC_MB_Type_PSKIP                       0x0
#define HENC_MB_Type_BSKIP_DIRECT                0x0
#define HENC_MB_Type_P16x16                      0x1
#define HENC_MB_Type_P16x8                       0x2
#define HENC_MB_Type_P8x16                       0x3
#define HENC_MB_Type_SMB8x8                      0x4
#define HENC_MB_Type_SMB8x4                      0x5
#define HENC_MB_Type_SMB4x8                      0x6
#define HENC_MB_Type_SMB4x4                      0x7
#define HENC_MB_Type_P8x8                        0x8
#define HENC_MB_Type_I4MB                        0x9
#define HENC_MB_Type_I16MB                       0xa
#define HENC_MB_Type_IBLOCK                      0xb
#define HENC_MB_Type_SI4MB                       0xc
#define HENC_MB_Type_I8MB                        0xd
#define HENC_MB_Type_IPCM                        0xe
#define HENC_MB_Type_AUTO                        0xf

#define ENCODER_BUFFER_INPUT              0
#define ENCODER_BUFFER_REF0                1
#define ENCODER_BUFFER_REF1                2
#define ENCODER_BUFFER_OUTPUT           3

#define I_FRAME   2 

#define SAD_MAX   65536

#define RANGE 80 // -32 ~ 32

typedef enum
{
    AMVENC_WRONG_STATE = -4,
    AMVENC_NOT_SUPPORTED = -3,
    AMVENC_MEMORY_FAIL = -2,
    AMVENC_FAIL = -1,
    /**
    Generic success value
    */
    AMVENC_SUCCESS = 0,
    AMVENC_PICTURE_READY = 1,
    AMVENC_NEW_IDR = 2, /* upon getting this, users have to call PVAVCEncodeSPS and PVAVCEncodePPS to get a new SPS and PPS*/
    AMVENC_SKIPPED_PICTURE = 3 /* continuable error message */

} AMVEnc_Status;

typedef unsigned int     uint_t;
typedef unsigned char    uint8_t;
//typedef unsigned __int64 uint64_t;
//typedef unsigned int uint64_t;

enum 
{ 
    P_8x8_UL, P_8x8_UR, P_8x8_DL, 
    P_8x8_DR, P_8x16_L, P_8x16_R,
    P_16x8_U, P_16x8_D, P_16x16_
};

enum
{
    P_8x8 = 1,
    P_16x8,
    P_8x16,
    P_16x16
}; 

typedef enum
{ 
    Slot_mode_idle = 0,
    Slot_mode_me,
    Slot_mode_I_fill,
    Slot_mode_P_fill
}SlotMode;

typedef struct 
{
    int frame; 
    uint8_t *plane[3];
}picture_t;

typedef struct
{
    int pix_width; 
    int pix_height;
	              
    int mb_width;   
    int mb_height;  
    int mbsize;
    
    bool nv21_data;
    picture_t pic[I_FRAME];
} yuv_t;

typedef struct  
{
    int x;
    int y;
    uint_t sad;
    int mad;
}mvs_t;

typedef struct  
{
    bool intra;
    unsigned mb_id;
    unsigned mb_type;
    int mvL0[16];
}mbinfo_t;

typedef struct{
    bool fullsearch_enable;

    int numIntraMB;
    int mvRange;

    int lambda_mode;
    int lambda_motion; 

    int totalSAD;

    uint8_t *mvbits_array;
    uint8_t *mvbits;
    mvs_t *mot16x16;
    int *min_cost;
    mbinfo_t* mb_array;
    uint8_t *yc_convert;    
}amvenc_motionsearch_t;

typedef struct{
    unsigned char* addr;
    unsigned size;
}amvenc_buff_t;

#define AMLNumI4PredMode  3
#define AMLNumI16PredMode  4
#define AMLNumIChromaMode  4


/**
Types of the macroblock and partition. PV Created.
@publishedAll
*/
typedef enum
{
    /* intra */
    AML_I4,
    AML_I16,
    AML_I_PCM,
    AML_SI4,

    /* inter for both P and B*/
    AML_BDirect16,
    AML_P16,
    AML_P16x8,
    AML_P8x16,
    AML_P8,
    AML_P8ref0,
    AML_SKIP
} AMLMBMode;

/**
Mode of intra 4x4 prediction. Table 8-2
@publishedAll
*/
typedef enum
{
    AML_I4_Vertical = 0,
    AML_I4_Horizontal,
    AML_I4_DC,
    AML_I4_Diagonal_Down_Left,
    AML_I4_Diagonal_Down_Right,
    AML_I4_Vertical_Right,
    AML_I4_Horizontal_Down,
    AML_I4_Vertical_Left,
    AML_I4_Horizontal_Up
} AMLIntra4x4PredMode;

/**
Mode of intra 16x16 prediction. Table 8-3
@publishedAll
*/
typedef enum
{
    AML_I16_Vertical = 0,
    AML_I16_Horizontal,
    AML_I16_DC,
    AML_I16_Plane
} AMLIntra16x16PredMode;

/**
This slice type follows Table 7-3. The bottom 5 items may not needed.
@publishedAll
*/
typedef enum
{
    AML_P_SLICE = 0,
    AML_B_SLICE,
    AML_I_SLICE,
    AML_SP_SLICE,
    AML_SI_SLICE,
    AML_P_ALL_SLICE,
    AML_B_ALL_SLICE,
    AML_I_ALL_SLICE,
    AML_SP_ALL_SLICE,
    AML_SI_ALL_SLICE,
} AMLSliceType;

typedef struct{
    int mbx;
    int mby;
    int mbAvailA;
    int mbAvailB;
    int mbAvailC;
    int mbAvailD;
}amvenc_neighbor_t;

/**
This structure contains macroblock related variables.
@publishedAll
*/
typedef struct tagMcrblck
{
    uint    mb_intra; /* intra flag */

    AMLMBMode mbMode;   /* type of MB prediction */
    AMLIntra16x16PredMode i16Mode; /* Intra16x16PredMode */
    AMLIntra4x4PredMode i4Mode[16]; /* Intra4x4PredMode, in raster scan order */
} AMLMacroblock;

typedef struct{

	AMLMacroblock *mblock;
	int	*min_cost;/* Minimum cost for the all MBs */

	int mb_width;
	int mb_height;
	int pitch;
	int height;

}amvenc_pred_mode_t;


typedef struct{

	/********* intra prediction scratch memory **********************/
	uint8_t	pred_i16[AMLNumI16PredMode][256]; /* save prediction for MB */
	uint8_t	pred_i4[AMLNumI4PredMode][16];	/* save prediction for blk */
	uint8_t	pred_ic[AMLNumIChromaMode][128];  /* for 2 chroma */

	int 	mostProbableI4Mode[16]; /* in raster scan order */

	AMLMacroblock *currMB;
	AMLSliceType slice_type;
	uint                mbNum; /* number of current MB */

    int mbAddrA, mbAddrB, mbAddrC, mbAddrD; /* address of neighboring MBs */

    amvenc_neighbor_t neighbor;

}amvenc_curr_pred_mode_t;

typedef struct{
    amvenc_pred_mode_t      *mb_list;
    amvenc_curr_pred_mode_t *mb_node;

    int lambda_mode;
    int lambda_motion;
    uint8_t *YCbCr[3];
}amvenc_pred_mode_obj_t;

void InitNeighborAvailability_iframe(amvenc_pred_mode_t *predMB,
					amvenc_curr_pred_mode_t* cur);


typedef struct{
    pthread_t mThread;
    sem_t semdone;
    sem_t semstart;

    int start_mbx;
    int start_mby;
    int end_mbx;
    int end_mby;
    int x_step;
    int y_step;
    unsigned update_bytes;
    SlotMode mode;
    int ret;
    bool finish;
    amvenc_pred_mode_obj_t *mbObj;
}amvenc_slot_t;

typedef struct{
    unsigned char* buff;
    unsigned char* y;
    unsigned char* uv;
    unsigned width;
    unsigned height;
    unsigned pitch;
    unsigned size;
}amvenc_reference_t;

typedef struct
{
    int fd;
    bool IDRframe;
    bool mStart;
    bool mCancel;
    yuv_t src;

    unsigned enc_width;
    unsigned enc_height;
    unsigned quant;

    unsigned char ref_id;

    bool gotSPS;
    unsigned sps_len;
    bool gotPPS;
    unsigned pps_len;

    unsigned PrevRefFrameNum;

    unsigned total_encode_frame;
    unsigned total_encode_time;

    amvenc_slot_t slot[2];

    amvenc_buff_t mmap_buff;
    amvenc_buff_t input_buf;
    amvenc_buff_t ref_buf_y[2];
    amvenc_buff_t ref_buf_uv[2];
    amvenc_buff_t output_buf;

    amvenc_reference_t ref_info;
    amvenc_motionsearch_t motion_search;
    amvenc_pred_mode_t intra_mode;
}amvenc_drv_t;

// asm for fill buffer
extern void fill_i_buffer_spec_neon(unsigned char* cur_mb_y  ,unsigned char* cur_mb_u  ,unsigned char* cur_mb_v  ,unsigned short* input_addr);
extern void Y_ext_asm2(uint8_t *ydst, uint8_t *ysrc, uint_t stride);
extern void YV12_UV_ext_asm2(unsigned short *dst, uint8_t *usrc, uint8_t *vsrc, uint_t stride);
extern void NV21_UV_ext_asm2(unsigned short *dst, uint8_t *uvsrc, uint_t stride);
extern void Y_line_asm(uint8_t *ydst, uint8_t *ysrc, uint_t pix_stride, uint_t mb_stride);
extern void nv21_uvline_asm(uint8_t *dst, uint8_t *uvsrc, uint_t pix_stride, uint_t mb_stride);
extern void yv12_uvline_asm(uint8_t *dst, uint8_t *usrc, uint8_t *vsrc, uint_t pix_stride, uint_t mb_stride);
extern void pY_ext_asm(unsigned short *ydst, uint8_t *ysrc, uint8_t *yref, uint_t stride, int offset);
extern void NV21_pUV_ext_asm(unsigned short *dst, uint8_t *uvsrc, uint8_t *uvref, uint_t stride, int ref_x);
extern void YV12_pUV_ext_asm(unsigned short *dst, uint8_t *usrc, uint8_t *vsrc,uint8_t *uvref, uint_t stride, int offset);

//Motion Estimation
extern void UpdateMotionQP(amvenc_drv_t* p);
extern void MBMotionSearch(amvenc_drv_t *p, int i0, int j0, int mbnum, bool FS_en);
extern void CleanMotionSearchMoudle(amvenc_motionsearch_t *motion_search);
extern AMVEnc_Status InitMotionSearchModule(amvenc_drv_t *p);
extern bool MVIntraDecisionABE(amvenc_motionsearch_t *motion_search, int *min_cost, uint8_t *cur, int pitch, bool ave);

extern amvenc_drv_t* InitAMVEncode(unsigned enc_width, unsigned enc_height, unsigned quant, unsigned mvrange, bool fs_en);
extern AMVEnc_Status AMVEncodeInitFrame(amvenc_drv_t* p, unsigned* yuv, bool IDRframe);
extern AMVEnc_Status AMVEncodeSPS_PPS(amvenc_drv_t* p, unsigned char* outptr,int* datalen);
//extern AMVEnc_Status AMVEncodePPS(amvenc_drv_t* p, unsigned char* outptr,int* datalen);
extern AMVEnc_Status AMVEncodeSlice(amvenc_drv_t* p, unsigned char* outptr,int* datalen);
extern void UnInitAMVEncode(amvenc_drv_t* p);

extern int InitMBIntraSearchModule( amvenc_drv_t *p);
extern int CleanMBIntraSearchModule( amvenc_drv_t *p);

#endif
