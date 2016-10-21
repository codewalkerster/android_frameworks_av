
//#define LOG_NDEBUG 0
#define LOG_TAG "ImagePlayerService"

#include "utils/Log.h"
#include "ImagePlayerService.h"

#include <stdlib.h>
#include <string.h>
#include <cutils/properties.h>
#include <utils/Errors.h>
#ifdef AM_KITKAT
#include <core/SkImageDecoder.h>
#include <core/SkData.h>
#else
#include <images/SkImageDecoder.h>
#endif

#include <core/SkCanvas.h>
#include <core/SkColorPriv.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryBase.h>
#include <media/stagefright/DataSource.h>
#include <assert.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

#include "RGBPicture.h"

#define CHECK assert
#define CHECK_EQ(a,b) CHECK((a)==(b))

#define Min(a, b) ((a) < (b) ? (a) : (b))

#define RET_OK                          0
#define RET_ERR_OPEN_SYSFS              -1
#define RET_ERR_OPEN_FILE               -2
#define RET_ERR_INVALID_OPERATION       -3
#define RET_ERR_DECORDER                -4
#define RET_ERR_PARAMETER               -5
#define RET_ERR_BAD_VALUE               -6
#define RET_ERR_NO_MEMORY               -7


#define SURFACE_4K_WIDTH            3840
#define SURFACE_4K_HEIGHT           2160

#define PICDEC_SYSFS                "/dev/picdec"
#define PICDEC_IOC_MAGIC            'P'
#define PICDEC_IOC_FRAME_RENDER     _IOW(PICDEC_IOC_MAGIC, 0x00, unsigned int)
#define PICDEC_IOC_FRAME_POST       _IOW(PICDEC_IOC_MAGIC,0X01,unsigned int)

#define VIDEO_LAYER_FORMAT_RGB      0
#define VIDEO_LAYER_FORMAT_RGBA     1
#define VIDEO_LAYER_FORMAT_ARGB     2

namespace android {

class SkHttpStream : public SkStreamRewindable {
public:
    SkHttpStream(const char url[] = NULL)
        : fURL(strdup(url)), dataSource(NULL), isConnect(false), haveRead(0) {
        connect();
    }

    virtual ~SkHttpStream() {
        if (dataSource != NULL) {
            dataSource.clear();
            dataSource = NULL;
            isConnect = false;
            haveRead = 0;
        }
        free(fURL);
    }

    bool connect() {
        dataSource = DataSource::CreateFromURI(fURL, NULL);
        if (dataSource == NULL) {
            isConnect = false;
            return false;
        } else {
            isConnect = true;
            return true;
        }
    }

    bool rewind() {
        if (dataSource != NULL) {
            dataSource.clear();
            dataSource = NULL;
            isConnect = false;
            haveRead = 0;
        }

        return connect();
    }

    size_t read(void* buffer, size_t size) {
        ssize_t ret;
        if ((buffer == NULL) && (size == 0)) {
            return getLength();
        }

        if ((buffer == NULL) && (size > 0)) {
            haveRead += size;
            return size;
        }

        if (isConnect && (dataSource != NULL) && (buffer != NULL)) {
            ret = dataSource->readAt(haveRead, buffer, size);
            if ((ret <= 0) || (ret > (int)size)) {
                return 0;
            }
            haveRead += ret;

            return ret;
        } else {
            return 0;
        }
    }

    size_t getLength() {
        off64_t size;
        if (isConnect && (dataSource != NULL)) {
            int ret = dataSource->getSize(&size);

            if (ERROR_UNSUPPORTED == ret) {
                return 8192;
            } else {
                return (size_t)size;
            }
        } else {
            return 0;
        }
    }

private:
    char *fURL;
    sp<DataSource> dataSource;
    bool isConnect;
    off64_t haveRead;
};

}  // namespace android

namespace {
using android::SkHttpStream;

static bool verifyBySkImageDecoder(SkStreamRewindable *stream, SkBitmap **bitmap) {
    SkImageDecoder::Format format = SkImageDecoder::kUnknown_Format;
    SkImageDecoder* codec = SkImageDecoder::Factory(stream);

    if (codec) {
        format = codec->getFormat();
        if (format != SkImageDecoder::kUnknown_Format) {
            if (bitmap != NULL) {
                *bitmap = new SkBitmap();
                codec->setSampleSize(1);
                codec->decode(stream, *bitmap,
                        SkBitmap::kARGB_8888_Config,
                        SkImageDecoder::kDecodeBounds_Mode);
            }
            delete codec;
            return true;
        }
        delete codec;
    }
    return false;
}

static bool isPhotoByExtenName(const char *url) {
    if (!url)
        return false;

    char *ptr=NULL;
    ptr = strrchr(url, '.');
    if (ptr == NULL) {
    	ALOGE("isPhotoByExtenName ptr is NULL!!!");
    	return false;
    }
    ptr = ptr + 1;

    if ((strcasecmp(ptr, "bmp") == 0)
        || (strncasecmp(ptr, "bmp?", 4) == 0)
        || (strcasecmp(ptr, "png") == 0)
        || (strncasecmp(ptr, "png?", 4) == 0)
        || (strcasecmp(ptr, "jpg") == 0)
        || (strncasecmp(ptr, "jpg?", 4) == 0)
        || (strcasecmp(ptr, "jpeg") == 0)
        || (strncasecmp(ptr, "jpeg?", 5) == 0)
        || (strcasecmp(ptr, "mpo") == 0)
        || (strncasecmp(ptr, "mpo?", 4) == 0)
        || (strcasecmp(ptr, "gif") == 0)
        || (strncasecmp(ptr, "gif?", 4) == 0)
        || (strcasecmp(ptr, "ico") == 0)
        || (strncasecmp(ptr, "ico?", 4) == 0)
        || (strcasecmp(ptr, "wbmp") == 0)
        || (strncasecmp(ptr, "wbmp?", 5) == 0)) {
        return true;
    } else {
        return false;
    }
}

static bool isFdSupportedBySkImageDecoder(int fd, SkBitmap **bitmap) {
    char buf[1024];
    snprintf(buf, 1024, "/proc/self/fd/%d", fd);

    int len;
    int size = 1024;
    char *url;
    url = (char *) calloc(size, sizeof(char));

    while (1) {
        if (!url)
            return false;
        len = readlink(buf, url, size - 1);
        if (len == -1)
            break;
        if (len < (size - 1))
            break;
        size *= 2;
        url = (char*)realloc(url, size);
    }

    if (len != -1) {
        url[len] = 0;
        bool ret = isPhotoByExtenName(url);
        free(url);
        if (!ret)
            return false;
    } else {
        free(url);
        return false;
    }

#ifdef AM_KITKAT
    SkAutoTUnref<SkData> data(SkData::NewFromFD(fd));
    if (data.get() == NULL) {
        return false;
    }
    SkMemoryStream *stream = new SkMemoryStream(data);
#else
    SkFDStream *stream = new SkFDStream(fd, false);
#endif

    bool ret = verifyBySkImageDecoder(stream, bitmap);
    delete stream;
    return ret;
}

static bool isSupportedBySkImageDecoder(const char *uri, SkBitmap **bitmap) {
    bool ret = isPhotoByExtenName(uri);
    if (!ret)
        return false;

    if (!strncasecmp("file://", uri, 7)) {
        SkFILEStream stream(uri + 7);
        return verifyBySkImageDecoder(&stream, bitmap);
    }

   // if (!strncasecmp("http://", uri, 7)) {
   //     SkHttpStream httpStream(uri);
   //     return verifyBySkImageDecoder(&httpStream, bitmap);
   // }

    return false;
}

static SkBitmap* cropBitmapRect(SkBitmap *srcBitmap, int x, int y, int width, int height) {
    SkBitmap *dstBitmap = NULL;
    dstBitmap = new SkBitmap();
    SkIRect r;

    r.set(x, y, x + width, y + height);
    srcBitmap->setAlphaType(kOpaque_SkAlphaType);
    srcBitmap->setIsVolatile(true);

    bool ret = srcBitmap->extractSubset(dstBitmap, r);

    srcBitmap->setAlphaType(kIgnore_SkAlphaType);
    srcBitmap->setIsVolatile(false);

    if (!ret) {
        delete dstBitmap;
        return NULL;
    }

    return dstBitmap;
}

static SkBitmap* cropAndFillBitmap(SkBitmap *srcBitmap, int dstWidth, int dstHeight) {
    if (srcBitmap == NULL)
        return NULL;

    SkBitmap *devBitmap = new SkBitmap();
    SkCanvas *canvas = NULL;

    devBitmap->setConfig(SkBitmap::kARGB_8888_Config, dstWidth, dstHeight);
    devBitmap->allocPixels();
    devBitmap->eraseARGB(0, 0, 0, 0);

    canvas = new SkCanvas(*devBitmap);

    int minWidth = Min(srcBitmap->width(), dstWidth);
    int minHeight = Min(srcBitmap->height(), dstHeight);
    int srcx = (srcBitmap->width() - minWidth) / 2;
    int srcy = (srcBitmap->height() - minHeight) / 2;
    int dstx = (dstWidth - minWidth) / 2;
    int dsty = (dstHeight - minHeight) / 2;

    SkPaint paint;
    paint.setFilterBitmap(true);
    SkRect dst = SkRect::MakeXYWH(dstx, dsty, minWidth, minHeight);

#ifdef AM_KITKAT
    SkRect src = SkRect::MakeXYWH(srcx, srcy, minWidth, minHeight);
    canvas->drawBitmapRectToRect(*srcBitmap, &src, dst, &paint);
#else
    SkIRect src = SkIRect::MakeXYWH(srcx, srcy, minWidth, minHeight);
    canvas->drawBitmapRect(*srcBitmap, &src, dst, &paint);
#endif

    delete canvas;

    return devBitmap;
}

static __inline int RGBToY(uint8_t r, uint8_t g, uint8_t b) {
    return (66 * r + 129 * g +  25 * b + 0x1080) >> 8;
}
static __inline int RGBToU(uint8_t r, uint8_t g, uint8_t b) {
    return (112 * b - 74 * g - 38 * r + 0x8080) >> 8;
}
static __inline int RGBToV(uint8_t r, uint8_t g, uint8_t b) {
    return (112 * r - 94 * g - 18 * b + 0x8080) >> 8;
}

static __inline void ARGBToYUV422Row_C(const uint8_t* src_argb,
                      uint8_t* dst_yuyv, int width) {
    for (int x = 0; x < width - 1; x += 2) {
        uint8_t ar = (src_argb[0] + src_argb[4]) >> 1;
        uint8_t ag = (src_argb[1] + src_argb[5]) >> 1;
        uint8_t ab = (src_argb[2] + src_argb[6]) >> 1;
        dst_yuyv[0] = RGBToY(src_argb[2], src_argb[1], src_argb[0]);
        dst_yuyv[1] = RGBToU(ar, ag, ab);
        dst_yuyv[2] = RGBToY(src_argb[6], src_argb[5], src_argb[4]);
        dst_yuyv[3] = RGBToV(ar, ag, ab);
        src_argb += 8;
        dst_yuyv += 4;
    }

    if (width & 1) {
        dst_yuyv[0] = RGBToY(src_argb[2], src_argb[1], src_argb[0]);
        dst_yuyv[1] = RGBToU(src_argb[2], src_argb[1], src_argb[0]);
        dst_yuyv[2] = 0x00;     // garbage, needs crop
        dst_yuyv[3] = RGBToV(src_argb[2], src_argb[1], src_argb[0]);
    }
}

static __inline void RGB565ToYUVRow_C(const uint8_t* src_rgb565,
                     uint8_t* dst_yuyv, int width) {
    const uint8_t* next_rgb565 = src_rgb565 + width * 2;
    for (int x = 0; x < width - 1; x += 2) {
        uint8_t b0 = src_rgb565[0] & 0x1f;
        uint8_t g0 = (src_rgb565[0] >> 5) | ((src_rgb565[1] & 0x07) << 3);
        uint8_t r0 = src_rgb565[1] >> 3;
        uint8_t b1 = src_rgb565[2] & 0x1f;
        uint8_t g1 = (src_rgb565[2] >> 5) | ((src_rgb565[3] & 0x07) << 3);
        uint8_t r1 = src_rgb565[3] >> 3;
        uint8_t b2 = next_rgb565[0] & 0x1f;
        uint8_t g2 = (next_rgb565[0] >> 5) | ((next_rgb565[1] & 0x07) << 3);
        uint8_t r2 = next_rgb565[1] >> 3;
        uint8_t b3 = next_rgb565[2] & 0x1f;
        uint8_t g3 = (next_rgb565[2] >> 5) | ((next_rgb565[3] & 0x07) << 3);
        uint8_t r3 = next_rgb565[3] >> 3;
        uint8_t b = (b0 + b1 + b2 + b3);  // 565 * 4 = 787.
        uint8_t g = (g0 + g1 + g2 + g3);
        uint8_t r = (r0 + r1 + r2 + r3);
        b = (b << 1) | (b >> 6);  // 787 -> 888.
        r = (r << 1) | (r >> 6);
        dst_yuyv[0] = RGBToY(r, g, b);
        dst_yuyv[1] = RGBToV(r, g, b);
        dst_yuyv[2] = RGBToY(r, g, b);
        dst_yuyv[3] = RGBToU(r, g, b);
        src_rgb565 += 4;
        next_rgb565 += 4;
        dst_yuyv += 4;
    }

    if (width & 1) {
        uint8_t b0 = src_rgb565[0] & 0x1f;
        uint8_t g0 = (src_rgb565[0] >> 5) | ((src_rgb565[1] & 0x07) << 3);
        uint8_t r0 = src_rgb565[1] >> 3;
        uint8_t b2 = next_rgb565[0] & 0x1f;
        uint8_t g2 = (next_rgb565[0] >> 5) | ((next_rgb565[1] & 0x07) << 3);
        uint8_t r2 = next_rgb565[1] >> 3;
        uint8_t b = (b0 + b2);  // 565 * 2 = 676.
        uint8_t g = (g0 + g2);
        uint8_t r = (r0 + r2);
        b = (b << 2) | (b >> 4);  // 676 -> 888
        g = (g << 1) | (g >> 6);
        r = (r << 2) | (r >> 4);
        dst_yuyv[0] = RGBToY(r, g, b);
        dst_yuyv[1] = RGBToV(r, g, b);
        dst_yuyv[2] = 0x00; // garbage, needs crop
        dst_yuyv[3] = RGBToU(r, g, b);
    }
}

static __inline void Index8ToYUV422Row_C(const uint8_t* src_argb,
                      uint8_t* dst_yuyv, int width, SkColorTable* table) {
    uint8_t ar = 0;
    uint8_t ag = 0;
    uint8_t ab = 0;
    SkPMColor pre = 0;
    SkPMColor late = 0;

    for (int x = 0; x < width - 1; x += 2) {
        pre = (*table)[src_argb[0]];
        late = (*table)[src_argb[1]];

        ar = (SkGetPackedR32(pre)  + SkGetPackedR32(late)) >> 1;
        ag = (SkGetPackedG32(pre) + SkGetPackedG32(late)) >> 1;
        ab = (SkGetPackedB32(pre) + SkGetPackedB32(late)) >> 1;

        dst_yuyv[0] = RGBToY(SkGetPackedB32(pre), SkGetPackedG32(pre), SkGetPackedR32(pre));
        dst_yuyv[1] = RGBToU(ar, ag, ab);
        dst_yuyv[2] = RGBToY(SkGetPackedB32(late), SkGetPackedG32(late), SkGetPackedR32(late));
        dst_yuyv[3] = RGBToV(ar, ag, ab);
        src_argb += 2;
        dst_yuyv += 4;
    }

    if (width & 1) {
        pre = (*table)[src_argb[0]];
        dst_yuyv[0] = RGBToY(SkGetPackedB32(pre) , SkGetPackedG32(pre), SkGetPackedR32(pre));
        dst_yuyv[1] = RGBToU(SkGetPackedB32(pre) , SkGetPackedG32(pre), SkGetPackedR32(pre));
        dst_yuyv[2] = 0x00;     // garbage, needs crop
        dst_yuyv[3] = RGBToV(SkGetPackedB32(pre) , SkGetPackedG32(pre), SkGetPackedR32(pre));
    }
}

static void chmodSysfs(const char *sysfs, int mode) {
    char sysCmd[1024];
	sprintf(sysCmd, "chmod %d %s", mode, sysfs);
	if (system(sysCmd)){
        ALOGE("exec cmd:%s fail\n", sysCmd);
    }
}
}  // anonymous namespace

namespace android {
void ImagePlayerService::instantiate() {
    android::status_t ret = defaultServiceManager()->addService(
            String16("image.player"), new ImagePlayerService());

    if (ret != android::OK) {
        ALOGE("Couldn't register image.player service!");
        //return -1;
    }
    ALOGI("instantiate add service result:%d", ret);

    chmodSysfs(PICDEC_SYSFS, 644);
}

ImagePlayerService::ImagePlayerService()
    : mWidth(0), mHeight(0), mBitmap(NULL), mSampleSize(1),
    mImageUrl(NULL), mDstBitmap(NULL),
    mFileDescription(-1), isAutoCrop(false),
    surfaceWidth(SURFACE_4K_WIDTH), surfaceHeight(SURFACE_4K_HEIGHT),
    mDisplayFd(-1){
}

ImagePlayerService::~ImagePlayerService() {
}

int ImagePlayerService::init() {
    mParameter = new InitParameter();
    mParameter->degrees = 0.0f;
    mParameter->scaleX = 1.0f;
    mParameter->scaleY = 1.0f;
    mParameter->cropX = 0;
    mParameter->cropY = 0;
    mParameter->cropWidth = SURFACE_4K_WIDTH;
    mParameter->cropHeight = SURFACE_4K_HEIGHT;

    if (mDisplayFd >= 0){
        close(mDisplayFd);
    }

    mDisplayFd = open(PICDEC_SYSFS, O_RDWR);
    if(mDisplayFd < 0){
        ALOGE("init: mDisplayFd(%d) failure error: '%s' (%d)", mDisplayFd, strerror(errno), errno);
        return RET_ERR_OPEN_SYSFS;
    }

#if 1//workround: need post a frame to video layer
    FrameInfo_t info;

    char* bitmap_addr = (char*)malloc(100 * 100 * 3);
    memset(bitmap_addr, 0, 100 * 100 * 3);
    info.pBuff = bitmap_addr;
    info.frame_width = 100;
    info.frame_height = 100;
    info.format = VIDEO_LAYER_FORMAT_RGB;
    info.rotate = 0;

    ioctl(mDisplayFd, PICDEC_IOC_FRAME_RENDER, &info);
    ioctl(mDisplayFd, PICDEC_IOC_FRAME_POST, NULL);

    free(bitmap_addr);
#endif
    
    ALOGI("init success display fd:%d", mDisplayFd);

    return RET_OK;
}

int ImagePlayerService::setDataSource(const char *uri) {
    Mutex::Autolock autoLock(mLock);

    //ALOGI("setDataSource uri:%s", uri);

    if (mBitmap != NULL) {
        delete mBitmap;
        mBitmap = NULL;
    }

    if (NULL != mImageUrl) {
        delete[] mImageUrl;
        mImageUrl = NULL;
    }
    mImageUrl = new char[1024];
    memset(mImageUrl, 0, 1024);

    if (!strncasecmp("file://", uri, 7)) {
        strncpy(mImageUrl, uri + 7, 1024 - 1);
    } else if (!strncasecmp("http://", uri, 7)) {
        strncpy(mImageUrl, uri, 1024 - 1);
    } else {
        delete[] mImageUrl;
        return RET_ERR_INVALID_OPERATION;
    }

    ALOGI("setDataSource mImageUrl:%s", mImageUrl);

    if (!isSupportedBySkImageDecoder(uri, &mBitmap)) {
        return RET_ERR_INVALID_OPERATION;
    }

    if (mBitmap != NULL) {
        mWidth = mBitmap->width();
        mHeight = mBitmap->height();
        delete mBitmap;
        mBitmap = NULL;
    }

    return RET_OK;
}

int ImagePlayerService::setDataSource(int fd, int64_t offset, int64_t length) {
    Mutex::Autolock autoLock(mLock);

    ALOGI("setDataSource fd:%d, offset:%d, length:%d", fd, (int)offset, (int)length);

    if (mBitmap != NULL) {
        delete mBitmap;
        mBitmap = NULL;
    }

    mFileDescription = dup(fd);

    if (!isFdSupportedBySkImageDecoder(fd, &mBitmap)) {
        return RET_ERR_INVALID_OPERATION;
    }

    if (mBitmap != NULL) {
        mWidth = mBitmap->width();
        mHeight = mBitmap->height();
        delete mBitmap;
        mBitmap = NULL;
    }

    return RET_OK;
}

int ImagePlayerService::setSampleSurfaceSize(int sampleSize, int surfaceW, int surfaceH) {
    mSampleSize = sampleSize;
    surfaceWidth = surfaceW;
    surfaceHeight = surfaceH;

    if(surfaceW > SURFACE_4K_WIDTH){
        surfaceWidth = SURFACE_4K_WIDTH;
    }

    if(surfaceH > SURFACE_4K_HEIGHT){
        surfaceHeight = SURFACE_4K_HEIGHT;
    }

    ALOGD("setSampleSurfaceSize sampleSize:%d, surfaceW:%d, surfaceH:%d",
        sampleSize, surfaceW, surfaceH);

    return RET_OK;
}

int ImagePlayerService::setRotate(float degrees, int autoCrop) {
    Mutex::Autolock autoLock(mLock);

    isAutoCrop = autoCrop != 0;
    ALOGD("setRotate degrees:%f, isAutoCrop:%d", degrees, isAutoCrop);


    SkBitmap *dstBitmap = NULL;
    dstBitmap = rotate(mBitmap, degrees);
    if (dstBitmap != NULL) {
        //delete mBitmap;
        //mBitmap = dstBitmap;

        ALOGD("After rotate, Width: %d, Height: %d", dstBitmap->width(), dstBitmap->height());
        show(dstBitmap);
        delete dstBitmap;
    } else {
        return RET_ERR_DECORDER;
    }
    return RET_OK;
}

int ImagePlayerService::setScale(float sx, float sy, int autoCrop) {
    Mutex::Autolock autoLock(mLock);

    isAutoCrop = autoCrop != 0;
    ALOGD("setScale sx:%f, sy:%f, isAutoCrop:%d", sx, sy, isAutoCrop);

    SkBitmap *dstBitmap = NULL;
    dstBitmap = scale(mBitmap, sx, sy);
    if (dstBitmap != NULL) {
        ALOGD("After scale, Width: %d, Height: %d", dstBitmap->width(), dstBitmap->height());
        show(dstBitmap);
        delete dstBitmap;
    } else {
        return RET_ERR_DECORDER;
    }
    return RET_OK;
}

int ImagePlayerService::setRotateScale(float degrees, float sx, float sy, int autoCrop) {
    Mutex::Autolock autoLock(mLock);

    isAutoCrop = autoCrop != 0;
    ALOGD("setRotateScale degrees:%f, sx:%f, sy:%f, isAutoCrop:%d", degrees, sx, sy, isAutoCrop);

    SkBitmap *dstBitmap = NULL;
    dstBitmap = rotateAndScale(mBitmap, degrees, sx, sy);
    if (dstBitmap != NULL) {
        ALOGD("After rotate and scale, Width: %d, Height: %d", dstBitmap->width(), dstBitmap->height());
        show(dstBitmap);
        delete dstBitmap;
    } else {
        return RET_ERR_DECORDER;
    }
    return RET_OK;
}

int ImagePlayerService::setCropRect(int cropX, int cropY, int cropWidth, int cropHeight) {
    Mutex::Autolock autoLock(mLock);

    ALOGD("setCropRect cropX:%d, cropY:%d, cropWidth:%d, cropHeight:%d", cropX, cropY, cropWidth, cropHeight);

    if (mBitmap == NULL) {
        ALOGD("Warning: mBitmap is NULL");
        return RET_ERR_BAD_VALUE;
    }

    if ((-1 < cropX) && (cropX < mBitmap->width()) && (-1 < cropY) && (cropY < mBitmap->height())
        && (0 < cropWidth) && (0 < cropHeight) && ((cropX + cropWidth) <= mBitmap->width())
        && ((cropY + cropHeight) <= mBitmap->height())) {

        showBitmapRect(mBitmap, cropX, cropY, cropWidth, cropHeight);
        /*
        SkBitmap *dstBitmap = NULL;
        dstBitmap = cropBitmapRect(mBitmap, cropX, cropY, cropWidth, cropHeight);
        if (dstBitmap != NULL) {
            show(dstBitmap);
            delete dstBitmap;
        } else {
            ALOGD("error: cropBitmapRect fail!");
            return BAD_VALUE;
        }*/
    } else {
        ALOGD("Warning: parameters is not valid");
        return RET_ERR_PARAMETER;
    }

    return RET_OK;
}

int ImagePlayerService::start() {
    ALOGI("start");

    prepare();
    show();
    return RET_OK;
}

int ImagePlayerService::release() {

    ALOGI("release");

    if (mBitmap != NULL) {
        if (mBitmap == mDstBitmap) {
            mDstBitmap = NULL;
        }
        delete mBitmap;
        mBitmap = NULL;
    }

    if (mDstBitmap != NULL) {
        delete mDstBitmap;
        mDstBitmap = NULL;
    }

    if (mImageUrl != NULL) {
        delete[] mImageUrl;
        mImageUrl = NULL;
    }

    delete mParameter;
    mParameter = NULL;

    if (mFileDescription >= 0){
        close(mFileDescription);
        mFileDescription = -1;
    }

    if (mDisplayFd >= 0){
        close(mDisplayFd);
        mDisplayFd = -1;
    }

    return RET_OK;
}

SkBitmap* ImagePlayerService::decode(SkStreamRewindable *stream, InitParameter *mParameter) {
    SkImageDecoder::Format format = SkImageDecoder::kUnknown_Format;
    SkImageDecoder* codec = NULL;
    bool ret = false;
    SkBitmap *bitmap = NULL;

    codec = SkImageDecoder::Factory(stream);
    if (codec) {
        format = codec->getFormat();
        if (format != SkImageDecoder::kUnknown_Format) {
            bitmap = new SkBitmap();
            if (mSampleSize > 0) {
                codec->setSampleSize(mSampleSize);
            } else {
                codec->setSampleSize(1);
            }

            ret = codec->decode(stream, bitmap, SkBitmap::kARGB_8888_Config, SkImageDecoder::kDecodePixels_Mode);
            if (!ret) {
                delete bitmap;
                bitmap = NULL;
            }
        } else {
            ALOGE("format is SkImageDecoder::kUnknown_Format!");
        }
        delete codec;
    } else {
        ALOGE("decode: codec is NULL!: '%s' (%d)", strerror(errno), errno);
        return NULL;
    }

    if (!ret) {
        ALOGE("error: decode fail!");
        return NULL;
    }
    //ALOGD("Decode output size, Width: %d, Height: %d", bitmap->width(), bitmap->height());

    if ((bitmap != NULL) && (mParameter != NULL)
        && ((mParameter->degrees != 0.0f) || (mParameter->scaleX != 1.0f) || (mParameter->scaleY != 1.0f))
        && (mParameter->scaleX > 0.0f) && (mParameter->scaleY > 0.0f)) {
        SkBitmap *dstBitmap = NULL;
        dstBitmap = rotateAndScale(bitmap, mParameter->degrees, mParameter->scaleX, mParameter->scaleY);

        if (dstBitmap != NULL) {
            delete bitmap;
            bitmap = dstBitmap;
        }
    }

    if ((bitmap != NULL) && (mParameter != NULL)) {
        SkBitmap *dstBitmap = NULL;
        dstBitmap = cropBitmapRect(bitmap, mParameter->cropX, mParameter->cropY, mParameter->cropWidth, mParameter->cropHeight);

        if (dstBitmap != NULL) {
            delete bitmap;
            bitmap = dstBitmap;
        }
    }

    if (bitmap != NULL ) {
        mWidth = bitmap->width();
        mHeight = bitmap->height();
        ALOGD("Image output size, Width:%d, Height:%d", mWidth, mHeight);
    }

    return bitmap;
}

SkBitmap* ImagePlayerService::scale(SkBitmap *srcBitmap, float sx, float sy) {
    if (srcBitmap == NULL)
        return NULL;

    int sourceWidth = srcBitmap->width();
    int sourceHeight = srcBitmap->height();
    int dstWidth = sourceWidth * sx;
    int dstHeight = sourceHeight * sy;
    if ((dstWidth <= 0) || (dstHeight <= 0)) {
        return NULL;
    }

    SkBitmap *devBitmap = new SkBitmap();
    SkMatrix *matrix = new SkMatrix();
    SkCanvas *canvas = NULL;

    devBitmap->setConfig(SkBitmap::kARGB_8888_Config, dstWidth, dstHeight);
    devBitmap->allocPixels();

    canvas = new SkCanvas(*devBitmap);

    matrix->postScale(sx, sy, 0, 0);

    canvas->drawBitmapMatrix(*srcBitmap, *matrix, NULL);

    delete canvas;
    delete matrix;

    return devBitmap;
}

SkBitmap* ImagePlayerService::rotate(SkBitmap *srcBitmap, float degrees) {
    if (srcBitmap == NULL)
        return NULL;

    SkBitmap *devBitmap = new SkBitmap();
    SkMatrix *matrix = new SkMatrix();
    SkCanvas *canvas = NULL;

    int sourceWidth = srcBitmap->width();
    int sourceHeight = srcBitmap->height();
    double radian = SkDegreesToRadians(degrees);

    int dstWidth = sourceWidth * fabs(cos(radian)) + sourceHeight * fabs(sin(radian));
    int dstHeight = sourceHeight * fabs(cos(radian)) + sourceWidth * fabs(sin(radian));

    devBitmap->setConfig(SkBitmap::kARGB_8888_Config, dstWidth, dstHeight);
    devBitmap->allocPixels();

    canvas = new SkCanvas(*devBitmap);

    matrix->postRotate(degrees, sourceWidth / 2, sourceHeight / 2);
    matrix->postTranslate((dstWidth - sourceWidth) / 2, (dstHeight - sourceHeight) / 2);

    canvas->drawBitmapMatrix(*srcBitmap, *matrix, NULL);

    delete canvas;
    delete matrix;

    return devBitmap;
}

SkBitmap* ImagePlayerService::rotateAndScale(SkBitmap *srcBitmap, float degrees, float sx, float sy) {
    if (srcBitmap == NULL)
        return NULL;

    int sourceWidth = srcBitmap->width();
    int sourceHeight = srcBitmap->height();
    double radian = SkDegreesToRadians(degrees);

    int dstWidthAfterRotate = sourceWidth * fabs(cos(radian)) + sourceHeight * fabs(sin(radian));
    int dstHeightAfterRotate = sourceHeight * fabs(cos(radian)) + sourceWidth * fabs(sin(radian));

    int dstWidthAfterScale = dstWidthAfterRotate * sx;
    int dstHeightAfterScale = dstHeightAfterRotate * sy;
    if ((dstWidthAfterScale <= 0) || (dstHeightAfterScale <= 0)) {
        return NULL;
    }

    SkBitmap *devBitmap = new SkBitmap();
    SkMatrix *matrix = new SkMatrix();
    SkCanvas *canvas = NULL;

    devBitmap->setConfig(SkBitmap::kARGB_8888_Config, dstWidthAfterScale, dstHeightAfterScale);
    devBitmap->allocPixels();

    canvas = new SkCanvas(*devBitmap);

    matrix->postRotate(degrees, sourceWidth / 2, sourceHeight / 2);
    matrix->postTranslate((dstWidthAfterRotate - sourceWidth) / 2, (dstHeightAfterRotate - sourceHeight) / 2);
    matrix->postScale(sx, sy, 0, 0);

    canvas->drawBitmapMatrix(*srcBitmap, *matrix, NULL);

    delete canvas;
    delete matrix;

    return devBitmap;
}

//render to video layer
int ImagePlayerService::prepare() {
    Mutex::Autolock autoLock(mLock);

    FrameInfo_t info;

    ALOGI("prepare image path:%s", mImageUrl);

    SkStreamRewindable *stream;
    if (mFileDescription >= 0) {
#ifdef AM_KITKAT
        SkAutoTUnref<SkData> data(SkData::NewFromFD(mFileDescription));
        if (data.get() == NULL) {
            return RET_ERR_BAD_VALUE;
        }
        stream = new SkMemoryStream(data);
#else
        stream = new SkFDStream(mFileDescription, false);
#endif
    //}else if (!strncasecmp("http://", mImageUrl, 7)) {
    //    stream = new SkHttpStream(mImageUrl);
    } else {
        stream = new SkFILEStream(mImageUrl);
    }

    if (mBitmap != NULL) {
        delete mBitmap;
        mBitmap = NULL;
    }

    /* for test open file permission
    int fileFd = open(mImageUrl, O_RDONLY);
    if(fileFd < 0){
        ALOGE("prepare: open (%s) failure error: '%s' (%d)", mImageUrl, strerror(errno), errno);
        return BAD_VALUE;
    }*/

    mBitmap = decode(stream, NULL);

    delete stream;
    if (mImageUrl != NULL) {
        delete[] mImageUrl;
        mImageUrl = NULL;
    }

    if (mBitmap == NULL){
        ALOGI("prepare decode result bitmap is NULL");
        return RET_ERR_BAD_VALUE;
    }

    if (mWidth <= 0 || mHeight <= 0){
        ALOGI("prepare decode result bitmap size error");
        return RET_ERR_BAD_VALUE;
    }

    if(mDisplayFd < 0){
        ALOGE("render, but displayFd can not ready");
        return RET_ERR_BAD_VALUE;
    }

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if(mWidth > surfaceWidth){
        scaleX = (float)surfaceWidth/mWidth;
    }

    if(mHeight > surfaceHeight){
        scaleY = (float)surfaceHeight/mHeight;
    }

    if(scaleX < scaleY) scaleY = scaleX;
    else if(scaleX > scaleY) scaleX = scaleY;

    if ((scaleX != 1.0f) || (scaleY != 1.0f)) {
        SkBitmap *dstBitmap = scale(mBitmap, scaleX, scaleY);
        if (dstBitmap != NULL) {
            delete mBitmap;
            mBitmap = dstBitmap;
        }

        ALOGD("prepare scale sx:%f, sy:%f", scaleX, scaleY);
    }

    render(VIDEO_LAYER_FORMAT_RGBA, mBitmap);
    ALOGI("prepare render is OK");
    return RET_OK;
}

int ImagePlayerService::render(int format, SkBitmap *bitmap){
    FrameInfo_t info;

    if(mDisplayFd < 0){
        ALOGE("render, but displayFd can not ready");
        return RET_ERR_BAD_VALUE;
    }

    if(NULL == bitmap){
        ALOGE("render, bitmap is NULL");
        return RET_ERR_BAD_VALUE;
    }

    ALOGI("render format:%d, bitmap w:%d, h;%d", format, bitmap->width(), bitmap->height());

    switch(format){
        case VIDEO_LAYER_FORMAT_RGB:{
            char* bitmapAddr = NULL;
            int len = bitmap->width()*bitmap->height()*3;//RGBA -> RGB
            bitmapAddr = (char*)malloc(len);
            if(NULL == bitmapAddr){
                ALOGE("render, not enough memory");
                return RET_ERR_NO_MEMORY;
            }
            memset(bitmapAddr, 0, len);

            bitmap->lockPixels();
            convertRGBA8888toRGB(bitmapAddr, bitmap);
            bitmap->unlockPixels();

            info.pBuff = bitmapAddr;
            info.format = format;
            info.frame_width = bitmap->width();
            info.frame_height = bitmap->height();

            ioctl(mDisplayFd, PICDEC_IOC_FRAME_RENDER, &info);

            if(NULL != bitmapAddr)
                free(bitmapAddr);
        }
        break;

        case VIDEO_LAYER_FORMAT_RGBA:{
            bitmap->lockPixels();
            info.pBuff = (char*)bitmap->getPixels();
            info.format = format;
            info.frame_width = bitmap->width();
            info.frame_height = bitmap->height();

            ioctl(mDisplayFd, PICDEC_IOC_FRAME_RENDER, &info); 
            bitmap->unlockPixels();
        }
        break;

        case VIDEO_LAYER_FORMAT_ARGB:
        default:
            break;
    }

    return RET_OK;
}

//post to display device
int ImagePlayerService::show() {
    if(mDisplayFd < 0){
        ALOGE("post, but displayFd can not ready");
        return RET_ERR_BAD_VALUE;
    }

    ALOGI("show picture display fd:%d", mDisplayFd);
    ioctl(mDisplayFd, PICDEC_IOC_FRAME_POST, NULL);

    return RET_OK;
}

bool ImagePlayerService::show(SkBitmap *bitmap){
    render(VIDEO_LAYER_FORMAT_RGBA, bitmap);
    show();
    return true;
}

bool ImagePlayerService::showBitmapRect(SkBitmap *bitmap, int cropX, int cropY, int cropWidth, int cropHeight){
    FrameInfo_t info;
    char* bitmapAddr = NULL;
    int len = cropWidth*cropHeight*3;//RGBA -> RGB
    bitmapAddr = (char*)malloc(len);
    if(NULL == bitmapAddr){
        ALOGE("showBitmapRect, not enough memory");
        return false;
    }
    memset(bitmapAddr, 0, len);

    uint8_t *pDst = (uint8_t*)bitmapAddr;
    uint8_t *pSrc = (uint8_t*)bitmap->getPixels();
    uint32_t u32DstStride = cropWidth*3;

    for (int y = 0; y < cropHeight; y++) {
        uint32_t srcOffset = bitmap->rowBytes()*(cropY + y) + 4*cropX;

        for (int x = 0; x < cropWidth; x++) {
            pDst[3*x+0] = pSrc[4*x+srcOffset+0];//B
            pDst[3*x+1] = pSrc[4*x+srcOffset+1];//G
            pDst[3*x+2] = pSrc[4*x+srcOffset+2];//R
                        //pSrc[4*x+3]; A
        }
        pDst += u32DstStride;
    }

    info.pBuff = bitmapAddr;
    info.format = VIDEO_LAYER_FORMAT_RGB;
    info.frame_width = cropWidth;
    info.frame_height = cropHeight;

    ioctl(mDisplayFd, PICDEC_IOC_FRAME_RENDER, &info);

    if(NULL != bitmapAddr)
        free(bitmapAddr);

    show();
    return true;
}


int ImagePlayerService::convertRGBA8888toRGB(void *dst, const SkBitmap *src) {
    uint8_t *pDst = (uint8_t*)dst;
    uint8_t *pSrc = (uint8_t*)src->getPixels();
    uint32_t u32SrcStride = src->rowBytes();
    uint32_t u32DstStride = src->width()*3;

    for (int y = 0; y < src->height(); y++) {
        for (int x = 0; x < src->width(); x++) {
            pDst[3*x+0] = pSrc[4*x+0];//B
            pDst[3*x+1] = pSrc[4*x+1];//G
            pDst[3*x+2] = pSrc[4*x+2];//R
                        //pSrc[4*x+3]; A
        }
        pSrc += u32SrcStride;
        pDst += u32DstStride;
    }

    return RET_OK;
}

int ImagePlayerService::convertARGB8888toYUYV(void *dst, const SkBitmap *src) {
    uint8_t *pDst = (uint8_t*)dst;
    uint8_t *pSrc = (uint8_t*)src->getPixels();
    uint32_t u32SrcStride = src->rowBytes();
    uint32_t u32DstStride = ((src->width() + 15) & ~15) * 2; //YUYV

    for (int y = 0; y < src->height(); y++) {
        ARGBToYUV422Row_C(pSrc, pDst, src->width());
        pSrc += u32SrcStride;
        pDst += u32DstStride;
    }

    return RET_OK;
}

int ImagePlayerService::convertRGB565toYUYV(void *dst, const SkBitmap *src) {
    uint8_t *pDst = (uint8_t*)dst;
    uint8_t *pSrc = (uint8_t*)src->getPixels();
    uint32_t u32SrcStride = src->rowBytes();
    uint32_t u32DstStride = ((src->width() + 15) & ~15) * 2; //YUYV

    for (int y = 0; y < src->height() - 1; y++) {
        RGB565ToYUVRow_C(pSrc, pDst, src->width());
        pSrc += u32SrcStride;
        pDst += u32DstStride;
    }

    return RET_OK;
}

int ImagePlayerService::convertIndex8toYUYV(void *dst, const SkBitmap *src) {
    uint8_t *pDst = (uint8_t*)dst;
    const uint8_t *pSrc = (const uint8_t *)src->getPixels();
    uint32_t u32SrcStride = src->rowBytes();
    uint32_t u32DstStride = ((src->width() + 15) & ~15) * 2; //YUYV
    SkColorTable* table = src->getColorTable();

    for (int y = 0; y < src->height(); y++) {
        Index8ToYUV422Row_C(pSrc, pDst, src->width(), table);
        pSrc += u32SrcStride;
        pDst += u32DstStride;
    }

    return RET_OK;
}

status_t ImagePlayerService::dump(int fd, const Vector<String16>& args){
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    if (checkCallingPermission(String16("android.permission.DUMP")) == false) {
        snprintf(buffer, SIZE, "Permission Denial: "
                "can't dump ImagePlayerService from pid=%d, uid=%d\n",
                IPCThreadState::self()->getCallingPid(),
                IPCThreadState::self()->getCallingUid());
        result.append(buffer);
    } else {
        Mutex::Autolock lock(mLock);

        result.appendFormat("ImagePlayerService: mImageUrl:%s, mWidth:%d, mHeight:%d\n",
                mImageUrl, mWidth, mHeight);
        result.appendFormat("ImagePlayerService: mSampleSize:%d, surfaceWidth:%d, surfaceHeight:%d\n",
                mSampleSize, surfaceWidth, surfaceHeight);
        /*
        int n = args.size();
        for (int i = 0; i + 1 < n; i++) {
            String16 verboseOption("-v");
            if (args[i] == verboseOption) {
                String8 levelStr(args[i+1]);
                int level = atoi(levelStr.string());
                result = String8::format("\nSetting log level to %d.\n", level);
                setLogLevel(level);
                write(fd, result.string(), result.size());
            }
        }*/
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

}

#if 0 //test code
/*
    char* bitmap_addr = NULL;
#if 1
    int len = mBitmap->width()*mBitmap->height()*3;//ARGB -> RGB
    bitmap_addr = (char*)malloc(len);
    if(NULL == bitmap_addr){
        ALOGE("render, not enough memory");
        return NO_MEMORY;
    }

    memset(bitmap_addr, 0, len);

#endif

    mBitmap->lockPixels();

#if 0
    int argbFd = open("/sdcard/argbdata", O_CREAT | O_RDWR, 0755);
    if(argbFd < 0){
        ALOGE("argbFd(%d) failure error: '%s' (%d)", argbFd, strerror(errno), errno);
        return BAD_VALUE;
    }
    write(argbFd, mBitmap->getPixels(), mBitmap->rowBytes()*mBitmap->height());
    close(argbFd);
#endif
    //ARGB2bmp((char *)mBitmap->getPixels(), mBitmap->width(), mBitmap->height());


    convertRGBA8888toRGB(bitmap_addr, mBitmap);
    //info.pBuff = (char*)mBitmap->getPixels();
    mBitmap->unlockPixels();

    info.pBuff = bitmap_addr;
    info.format = VIDEO_LAYER_FORMAT_RGB;
    info.frame_width = mBitmap->width();
    info.frame_height = mBitmap->height();

    ALOGI("prepare display fd:%d, frame_width:%d, frame_height:%d", mDisplayFd, info.frame_width, info.frame_height);
    ioctl(mDisplayFd, PICDEC_IOC_FRAME_RENDER, &info); 

#if 0
    int rgbFd = open("/sdcard/rgbdata", O_CREAT | O_RDWR, 0755);
    if(rgbFd < 0){
        ALOGE("rgbFd(%d) failure error: '%s' (%d)", rgbFd, strerror(errno), errno);
        return BAD_VALUE;
    }
    write(rgbFd, info.pBuff, len);
    close(rgbFd);
#endif

    if(NULL != bitmap_addr)
        free(bitmap_addr);

*/
#endif
