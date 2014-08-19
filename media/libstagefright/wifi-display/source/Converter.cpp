/*
 * Copyright 2012, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "Converter"
#include <utils/Log.h>

#include "Converter.h"

#include "MediaPuller.h"
#include "include/avc_utils.h"

#include <cutils/properties.h>
#include <gui/Surface.h>
#include <media/ICrypto.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>

#include <arpa/inet.h>

#include <OMX_Video.h>

#ifdef USES_WFD_SERVICE
#include <media/WFDService.h>
#include <media/WFDServiceListener.h>
#endif

#ifdef USES_WIFI_DISPLAY
#include <media/stagefright/MetaData.h>
#endif

#define MAX_WAIT_TIME_US        100000

namespace android {

Converter::Converter(
        const sp<AMessage> &notify,
        const sp<ALooper> &codecLooper,
        const sp<AMessage> &outputFormat,
        uint32_t flags)
    : mNotify(notify),
      mCodecLooper(codecLooper),
      mOutputFormat(outputFormat),
#ifdef USES_WFD_SERVICE
      mState(INTERNAL_STATE_UNINITIALIZED),
      mPenddingOutputBufferCount(0),
      mIsSecure(false),
      mWfdServiceListener(NULL),
#endif
      mFlags(flags),
      mIsVideo(false),
      mIsH264(false),
      mIsPCMAudio(false),
      mNeedToManuallyPrependSPSPPS(false),
      mDoMoreWorkPending(false)
#if ENABLE_SILENCE_DETECTION
      ,mFirstSilentFrameUs(-1ll)
      ,mInSilentMode(false)
#endif
      ,mPrevVideoBitrate(-1)
      ,mNumFramesToDrop(0)
      ,mEncodingSuspended(false)
    {
    AString mime;
    CHECK(mOutputFormat->findString("mime", &mime));

    if (!strncasecmp("video/", mime.c_str(), 6)) {
        mIsVideo = true;

        mIsH264 = !strcasecmp(mime.c_str(), MEDIA_MIMETYPE_VIDEO_AVC);
    } else if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_RAW, mime.c_str())) {
        mIsPCMAudio = true;
    }
#ifdef USES_WFD_SERVICE
    if (mIsVideo) {
        status_t err = makeWFDServiceListener();
        if (err != OK)
            ALOGE("makeWFDServiceListener FAIL");
    }
    mInputFormat = outputFormat->dup();
#endif
}

static void ReleaseMediaBufferReference(const sp<ABuffer> &accessUnit) {
    void *mbuf;
    if (accessUnit->meta()->findPointer("mediaBuffer", &mbuf)
            && mbuf != NULL) {
        ALOGV("releasing mbuf %p", mbuf);

        accessUnit->meta()->setPointer("mediaBuffer", NULL);

        static_cast<MediaBuffer *>(mbuf)->release();
        mbuf = NULL;
    }
}

void Converter::releaseEncoder() {
    if (mEncoder == NULL) {
        return;
    }

    mEncoder->release();
    mEncoder.clear();

    while (!mInputBufferQueue.empty()) {
        sp<ABuffer> accessUnit = *mInputBufferQueue.begin();
        mInputBufferQueue.erase(mInputBufferQueue.begin());

        ReleaseMediaBufferReference(accessUnit);
    }

    for (size_t i = 0; i < mEncoderInputBuffers.size(); ++i) {
        sp<ABuffer> accessUnit = mEncoderInputBuffers.itemAt(i);
        ReleaseMediaBufferReference(accessUnit);
    }

    mEncoderInputBuffers.clear();
    mEncoderOutputBuffers.clear();
}

Converter::~Converter() {
    CHECK(mEncoder == NULL);
#ifdef USES_WFD_SERVICE
    if (mWfdServiceListener != NULL) {
        sp<IServiceManager> sm = defaultServiceManager();
        sp<IBinder> binder = sm->getService(String16("media.wifi_display"));
        sp<IWFDService> wfdService = interface_cast<IWFDService>(binder);
        wfdService->removeWFDServiceListener(mWfdServiceListener);
    }
#endif
}

void Converter::shutdownAsync() {
    ALOGV("shutdown");
    (new AMessage(kWhatShutdown, id()))->post();
}

status_t Converter::init() {
#ifdef USES_WFD_SERVICE
    mPenddingOutputBufferCount = 0;
    mIsSecure = false;
    mState = INTERNAL_STATE_INITIALIZED;
#endif
    status_t err = initEncoder();

    if (err != OK) {
        releaseEncoder();
    }

    return err;
}

#ifdef USES_WFD_SERVICE
void Converter::init(bool IsSecure)
{
    ALOGV("init secure %d", IsSecure);
    mPenddingOutputBufferCount = 0;
    mState = INTERNAL_STATE_UNINITIALIZED;
    mIsSecure = IsSecure;
    mOutputFormat = mInputFormat->dup();
    status_t err = initEncoder();
    if (err == OK) {
        mState = INTERNAL_STATE_INITIALIZED;
    } else {
        ALOGE("initEncoder failed");
        releaseEncoder();
    }
}

void Converter::internalStop() {
    ALOGV("internalStop mState %d", mState);
    if (mState == INTERNAL_STATE_INITIALIZED) {
        mState = INTERNAL_STATE_STOPPING;
        if (mPenddingOutputBufferCount == 0) {
            mState = INTERNAL_STATE_STOPPED;
        } else {
            unsigned int waitTimeUs = 0;
            while ((waitTimeUs < MAX_WAIT_TIME_US) &&
                    mState == INTERNAL_STATE_STOPPING) {
                usleep(10000);
                waitTimeUs += 10000;
            }
        }
    } else {
        ALOGE("internalStop() called on wrong state");
    }
}

void Converter::internalShutdown() {
    ALOGV("internalShutdown mState %d", mState);
    while (mAvailEncoderInputIndices.size()) {
        size_t bufferIndex = *mAvailEncoderInputIndices.begin();
        mAvailEncoderInputIndices.erase(mAvailEncoderInputIndices.begin());
        ALOGE("internalShutdown(), erase bufferIndex %d, mAvailEncoderInputIndices size %d",
            bufferIndex, mAvailEncoderInputIndices.size());
    }
    while (mInputBufferQueue.size()) {
        sp<ABuffer> buffer = *mInputBufferQueue.begin();
        mInputBufferQueue.erase(mInputBufferQueue.begin());
        ALOGE("internalShutdown(), erase buffer %p, mInputBufferQueue size %d",
                    buffer.get(), mInputBufferQueue.size());
    }
    sp<ABuffer> csdBuffer = NULL;
#if 0
    /* Todo: add CSD buffer handling code for secure playback */
    if (mOutputFormat->findBuffer("csd-0", &csdBuffer) &&
        csdBuffer != NULL) {
        ion_unmap(csdBuffer->data(), csdBuffer->size());
    }
#endif
    sp<AMessage> msg, response;
    msg = new AMessage(kWhatInternalShutdown, id());
    msg->postAndAwaitResponse(&response);

    mDoMoreWorkPending = false;
}
#endif

sp<IGraphicBufferProducer> Converter::getGraphicBufferProducer() {
    CHECK(mFlags & FLAG_USE_SURFACE_INPUT);
    return mGraphicBufferProducer;
}

size_t Converter::getInputBufferCount() const {
    return mEncoderInputBuffers.size();
}

sp<AMessage> Converter::getOutputFormat() const {
    return mOutputFormat;
}

bool Converter::needToManuallyPrependSPSPPS() const {
    return mNeedToManuallyPrependSPSPPS;
}

// static
int32_t Converter::GetInt32Property(
        const char *propName, int32_t defaultValue) {
    char val[PROPERTY_VALUE_MAX];
    if (property_get(propName, val, NULL)) {
        char *end;
        unsigned long x = strtoul(val, &end, 10);

        if (*end == '\0' && end > val && x > 0) {
            return x;
        }
    }

    return defaultValue;
}

status_t Converter::initEncoder() {
    AString outputMIME;
    CHECK(mOutputFormat->findString("mime", &outputMIME));

    bool isAudio = !strncasecmp(outputMIME.c_str(), "audio/", 6);

    if (!mIsPCMAudio) {
#ifdef USES_WFD_SERVICE
        if (isAudio) {
            mEncoder = MediaCodec::CreateByType(
                    mCodecLooper, outputMIME.c_str(), true /* encoder */);
        } else {
            if (mIsSecure) {
                mEncoder = MediaCodec::CreateByComponentName(
                        mCodecLooper, "OMX.Exynos.AVC.Encoder.secure");
            } else {
                mEncoder = MediaCodec::CreateByComponentName(
                        mCodecLooper, "OMX.Exynos.AVC.Encoder");
            }
        }
#else
        mEncoder = MediaCodec::CreateByType(
                mCodecLooper, outputMIME.c_str(), true /* encoder */);
#endif
        if (mEncoder == NULL) {
            return ERROR_UNSUPPORTED;
        }
    }

    if (mIsPCMAudio) {
        return OK;
    }

    int32_t audioBitrate = GetInt32Property("media.wfd.audio-bitrate", 128000);
    int32_t videoBitrate = GetInt32Property("media.wfd.video-bitrate", 5000000);
    mPrevVideoBitrate = videoBitrate;

    ALOGI("using audio bitrate of %d bps, video bitrate of %d bps",
          audioBitrate, videoBitrate);

    if (isAudio) {
        mOutputFormat->setInt32("bitrate", audioBitrate);
    } else {
        mOutputFormat->setInt32("bitrate", videoBitrate);
        mOutputFormat->setInt32("bitrate-mode", OMX_Video_ControlRateConstant);
        mOutputFormat->setInt32("frame-rate", 30);
        mOutputFormat->setInt32("i-frame-interval", 15);  // Iframes every 15 secs

        // Configure encoder to use intra macroblock refresh mode
        mOutputFormat->setInt32("intra-refresh-mode", OMX_VIDEO_IntraRefreshCyclic);

        int width, height, mbs;
        if (!mOutputFormat->findInt32("width", &width)
                || !mOutputFormat->findInt32("height", &height)) {
            return ERROR_UNSUPPORTED;
        }

        // Update macroblocks in a cyclic fashion with 10% of all MBs within
        // frame gets updated at one time. It takes about 10 frames to
        // completely update a whole video frame. If the frame rate is 30,
        // it takes about 333 ms in the best case (if next frame is not an IDR)
        // to recover from a lost/corrupted packet.
        mbs = (((width + 15) / 16) * ((height + 15) / 16) * 10) / 100;
        mOutputFormat->setInt32("intra-refresh-CIR-mbs", mbs);
    }

    ALOGV("output format is '%s'", mOutputFormat->debugString(0).c_str());

    mNeedToManuallyPrependSPSPPS = false;

    status_t err = NO_INIT;

    if (!isAudio) {
        sp<AMessage> tmp = mOutputFormat->dup();
        tmp->setInt32("prepend-sps-pps-to-idr-frames", 1);

        err = mEncoder->configure(
                tmp,
                NULL /* nativeWindow */,
                NULL /* crypto */,
                MediaCodec::CONFIGURE_FLAG_ENCODE);

        if (err == OK) {
            // Encoder supported prepending SPS/PPS, we don't need to emulate
            // it.
            mOutputFormat = tmp;
        } else {
            mNeedToManuallyPrependSPSPPS = true;

            ALOGI("We going to manually prepend SPS and PPS to IDR frames.");
        }
    }

    if (err != OK) {
        // We'll get here for audio or if we failed to configure the encoder
        // to automatically prepend SPS/PPS in the case of video.

        err = mEncoder->configure(
                    mOutputFormat,
                    NULL /* nativeWindow */,
                    NULL /* crypto */,
                    MediaCodec::CONFIGURE_FLAG_ENCODE);
    }

    if (err != OK) {
        return err;
    }

    if (mFlags & FLAG_USE_SURFACE_INPUT) {
        CHECK(mIsVideo);

        err = mEncoder->createInputSurface(&mGraphicBufferProducer);

        if (err != OK) {
            return err;
        }
    }

    err = mEncoder->start();

    if (err != OK) {
        return err;
    }

    err = mEncoder->getInputBuffers(&mEncoderInputBuffers);

    if (err != OK) {
        return err;
    }

    err = mEncoder->getOutputBuffers(&mEncoderOutputBuffers);

    if (err != OK) {
        return err;
    }

    if (mFlags & FLAG_USE_SURFACE_INPUT) {
        scheduleDoMoreWork();
    }

    return OK;
}

void Converter::notifyError(status_t err) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatError);
    notify->setInt32("err", err);
    notify->post();
}

// static
bool Converter::IsSilence(const sp<ABuffer> &accessUnit) {
    const uint8_t *ptr = accessUnit->data();
    const uint8_t *end = ptr + accessUnit->size();
    while (ptr < end) {
        if (*ptr != 0) {
            return false;
        }
        ++ptr;
    }

    return true;
}

void Converter::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatMediaPullerNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));
#ifdef USES_WIFI_DISPLAY
            bool modeChange = false;

            /* Todo: add buffer handling code for secure playback */
            if (what == MediaPuller::kWhatAccessUnit) {
                int32_t usage = 0;
                void *mbuf;
                sp<ABuffer> accessUnitForUsage;
                CHECK(msg->findBuffer("accessUnit", &accessUnitForUsage));

                if (accessUnitForUsage->meta()->findPointer("mediaBuffer", &mbuf)
                        && mbuf != NULL) {
                    if (((MediaBuffer *)(mbuf))->meta_data()->findInt32(kKeyIsDRM, &usage)) {
                        if (mIsSecure != !!(usage & GRALLOC_USAGE_PROTECTED)) {
                            modeChange = true;
                            ALOGV("encoder's input buffer changed. modeChange: %s",
                                    modeChange == true ? "TRUE" : "FALSE");
                        }
                    }
                }
            }

            if ((!mIsPCMAudio && mEncoder == NULL) ||
                mState == INTERNAL_STATE_STOPPING ||
                mState == INTERNAL_STATE_STOPPED ||
                modeChange) {
#else
            if (!mIsPCMAudio && mEncoder == NULL) {
#endif
                ALOGV("got msg '%s' after encoder shutdown.",
                      msg->debugString().c_str());

                if (what == MediaPuller::kWhatAccessUnit) {
                    sp<ABuffer> accessUnit;
                    CHECK(msg->findBuffer("accessUnit", &accessUnit));

                    ReleaseMediaBufferReference(accessUnit);
                }
                break;
            }

            if (what == MediaPuller::kWhatEOS) {
                mInputBufferQueue.push_back(NULL);

                feedEncoderInputBuffers();

                scheduleDoMoreWork();
            } else {
                CHECK_EQ(what, MediaPuller::kWhatAccessUnit);

                sp<ABuffer> accessUnit;
                CHECK(msg->findBuffer("accessUnit", &accessUnit));

                if (mNumFramesToDrop > 0 || mEncodingSuspended) {
                    if (mNumFramesToDrop > 0) {
                        --mNumFramesToDrop;
                        ALOGI("dropping frame.");
                    }

                    ReleaseMediaBufferReference(accessUnit);
                    break;
                }
#if 0
                void *mbuf;
                if (accessUnit->meta()->findPointer("mediaBuffer", &mbuf)
                        && mbuf != NULL) {
                    ALOGI("queueing mbuf %p", mbuf);
                }
#endif

#if ENABLE_SILENCE_DETECTION
                if (!mIsVideo) {
                    if (IsSilence(accessUnit)) {
                        if (mInSilentMode) {
                            break;
                        }

                        int64_t nowUs = ALooper::GetNowUs();

                        if (mFirstSilentFrameUs < 0ll) {
                            mFirstSilentFrameUs = nowUs;
                        } else if (nowUs >= mFirstSilentFrameUs + 10000000ll) {
                            mInSilentMode = true;
                            ALOGI("audio in silent mode now.");
                            break;
                        }
                    } else {
                        if (mInSilentMode) {
                            ALOGI("audio no longer in silent mode.");
                        }
                        mInSilentMode = false;
                        mFirstSilentFrameUs = -1ll;
                    }
                }
#endif

                mInputBufferQueue.push_back(accessUnit);

                feedEncoderInputBuffers();

                scheduleDoMoreWork();
            }
            break;
        }

        case kWhatEncoderActivity:
        {
#if 0
            int64_t whenUs;
            if (msg->findInt64("whenUs", &whenUs)) {
                int64_t nowUs = ALooper::GetNowUs();
                ALOGI("[%s] kWhatEncoderActivity after %lld us",
                      mIsVideo ? "video" : "audio", nowUs - whenUs);
            }
#endif

            mDoMoreWorkPending = false;

            if (mEncoder == NULL) {
                break;
            }

            status_t err = doMoreWork();

            if (err != OK) {
                notifyError(err);
            } else {
                scheduleDoMoreWork();
            }
            break;
        }

        case kWhatRequestIDRFrame:
        {
            if (mEncoder == NULL) {
                break;
            }

            if (mIsVideo) {
                ALOGV("requesting IDR frame");
                mEncoder->requestIDRFrame();
            }
            break;
        }

        case kWhatShutdown:
        {
            ALOGI("shutting down %s encoder", mIsVideo ? "video" : "audio");

            releaseEncoder();

            AString mime;
            CHECK(mOutputFormat->findString("mime", &mime));
            ALOGI("encoder (%s) shut down.", mime.c_str());

            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatShutdownCompleted);
            notify->post();
            break;
        }
#ifdef USES_WFD_SERVICE
        case kWhatInternalShutdown:
        {
            uint32_t replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            releaseEncoder();

            AString mime;
            CHECK(mOutputFormat->findString("mime", &mime));
            ALOGV("encoder (%s) shut down.", mime.c_str());

            sp<AMessage> response = new AMessage;
            response->setInt32("err", OK);
            response->postReply(replyID);
            break;
        }
#endif
        case kWhatDropAFrame:
        {
            ++mNumFramesToDrop;
            break;
        }

        case kWhatReleaseOutputBuffer:
        {
            if (mEncoder != NULL) {
#ifdef USES_WFD_SERVICE
                mPenddingOutputBufferCount--;
                if ((mState == INTERNAL_STATE_STOPPING) && (mPenddingOutputBufferCount == 0))
                    mState = INTERNAL_STATE_STOPPED;
#endif
                size_t bufferIndex;
                CHECK(msg->findInt32("bufferIndex", (int32_t*)&bufferIndex));
                CHECK(bufferIndex < mEncoderOutputBuffers.size());
                mEncoder->releaseOutputBuffer(bufferIndex);
            }
            break;
        }

        case kWhatSuspendEncoding:
        {
            int32_t suspend;
            CHECK(msg->findInt32("suspend", &suspend));

            mEncodingSuspended = suspend;

            if (mFlags & FLAG_USE_SURFACE_INPUT) {
                sp<AMessage> params = new AMessage;
                params->setInt32("drop-input-frames",suspend);
                mEncoder->setParameters(params);
            }
            break;
        }

        default:
            TRESPASS();
    }
}

void Converter::scheduleDoMoreWork() {
    if (mIsPCMAudio) {
        // There's no encoder involved in this case.
        return;
    }

    if (mDoMoreWorkPending) {
        return;
    }

    mDoMoreWorkPending = true;

#if 1
    if (mEncoderActivityNotify == NULL) {
        mEncoderActivityNotify = new AMessage(kWhatEncoderActivity, id());
    }
    mEncoder->requestActivityNotification(mEncoderActivityNotify->dup());
#else
    sp<AMessage> notify = new AMessage(kWhatEncoderActivity, id());
    notify->setInt64("whenUs", ALooper::GetNowUs());
    mEncoder->requestActivityNotification(notify);
#endif
}

status_t Converter::feedRawAudioInputBuffers() {
    // Split incoming PCM audio into buffers of 6 AUs of 80 audio frames each
    // and add a 4 byte header according to the wifi display specs.

    while (!mInputBufferQueue.empty()) {
        sp<ABuffer> buffer = *mInputBufferQueue.begin();
        mInputBufferQueue.erase(mInputBufferQueue.begin());

        int16_t *ptr = (int16_t *)buffer->data();
        int16_t *stop = (int16_t *)(buffer->data() + buffer->size());
        while (ptr < stop) {
            *ptr = htons(*ptr);
            ++ptr;
        }

        static const size_t kFrameSize = 2 * sizeof(int16_t);  // stereo
        static const size_t kFramesPerAU = 80;
        static const size_t kNumAUsPerPESPacket = 6;

        if (mPartialAudioAU != NULL) {
            size_t bytesMissingForFullAU =
                kNumAUsPerPESPacket * kFramesPerAU * kFrameSize
                - mPartialAudioAU->size() + 4;

            size_t copy = buffer->size();
            if(copy > bytesMissingForFullAU) {
                copy = bytesMissingForFullAU;
            }

            memcpy(mPartialAudioAU->data() + mPartialAudioAU->size(),
                   buffer->data(),
                   copy);

            mPartialAudioAU->setRange(0, mPartialAudioAU->size() + copy);

            buffer->setRange(buffer->offset() + copy, buffer->size() - copy);

            int64_t timeUs;
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            int64_t copyUs = (int64_t)((copy / kFrameSize) * 1E6 / 48000.0);
            timeUs += copyUs;
            buffer->meta()->setInt64("timeUs", timeUs);

            if (bytesMissingForFullAU == copy) {
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatAccessUnit);
                notify->setBuffer("accessUnit", mPartialAudioAU);
                notify->post();

                mPartialAudioAU.clear();
            }
        }

        while (buffer->size() > 0) {
            sp<ABuffer> partialAudioAU =
                new ABuffer(
                        4
                        + kNumAUsPerPESPacket * kFrameSize * kFramesPerAU);

            uint8_t *ptr = partialAudioAU->data();
            ptr[0] = 0xa0;  // 10100000b
            ptr[1] = kNumAUsPerPESPacket;
            ptr[2] = 0;  // reserved, audio _emphasis_flag = 0

            static const unsigned kQuantizationWordLength = 0;  // 16-bit
            static const unsigned kAudioSamplingFrequency = 2;  // 48Khz
            static const unsigned kNumberOfAudioChannels = 1;  // stereo

            ptr[3] = (kQuantizationWordLength << 6)
                    | (kAudioSamplingFrequency << 3)
                    | kNumberOfAudioChannels;

            size_t copy = buffer->size();
            if (copy > partialAudioAU->size() - 4) {
                copy = partialAudioAU->size() - 4;
            }

            memcpy(&ptr[4], buffer->data(), copy);

            partialAudioAU->setRange(0, 4 + copy);
            buffer->setRange(buffer->offset() + copy, buffer->size() - copy);

            int64_t timeUs;
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            partialAudioAU->meta()->setInt64("timeUs", timeUs);

            int64_t copyUs = (int64_t)((copy / kFrameSize) * 1E6 / 48000.0);
            timeUs += copyUs;
            buffer->meta()->setInt64("timeUs", timeUs);

            if (copy == partialAudioAU->capacity() - 4) {
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatAccessUnit);
                notify->setBuffer("accessUnit", partialAudioAU);
                notify->post();

                partialAudioAU.clear();
                continue;
            }

            mPartialAudioAU = partialAudioAU;
        }
    }

    return OK;
}

status_t Converter::feedEncoderInputBuffers() {
    if (mIsPCMAudio) {
        return feedRawAudioInputBuffers();
    }

    while (!mInputBufferQueue.empty()
            && !mAvailEncoderInputIndices.empty()) {
        sp<ABuffer> buffer = *mInputBufferQueue.begin();
        mInputBufferQueue.erase(mInputBufferQueue.begin());

        size_t bufferIndex = *mAvailEncoderInputIndices.begin();
        mAvailEncoderInputIndices.erase(mAvailEncoderInputIndices.begin());

        int64_t timeUs = 0ll;
        uint32_t flags = 0;

        if (buffer != NULL) {
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            memcpy(mEncoderInputBuffers.itemAt(bufferIndex)->data(),
                   buffer->data(),
                   buffer->size());

            void *mediaBuffer;
            if (buffer->meta()->findPointer("mediaBuffer", &mediaBuffer)
                    && mediaBuffer != NULL) {
                mEncoderInputBuffers.itemAt(bufferIndex)->meta()
                    ->setPointer("mediaBuffer", mediaBuffer);

                buffer->meta()->setPointer("mediaBuffer", NULL);
            }
        } else {
            flags = MediaCodec::BUFFER_FLAG_EOS;
        }

        status_t err = mEncoder->queueInputBuffer(
                bufferIndex, 0, (buffer == NULL) ? 0 : buffer->size(),
                timeUs, flags);

        if (err != OK) {
            return err;
        }
    }

    return OK;
}

sp<ABuffer> Converter::prependCSD(const sp<ABuffer> &accessUnit) const {
    CHECK(mCSD0 != NULL);

    sp<ABuffer> dup = new ABuffer(accessUnit->size() + mCSD0->size());
    memcpy(dup->data(), mCSD0->data(), mCSD0->size());
    memcpy(dup->data() + mCSD0->size(), accessUnit->data(), accessUnit->size());

    int64_t timeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

    dup->meta()->setInt64("timeUs", timeUs);

    return dup;
}

status_t Converter::doMoreWork() {
    status_t err;

    if (!(mFlags & FLAG_USE_SURFACE_INPUT)) {
        for (;;) {
            size_t bufferIndex;
            err = mEncoder->dequeueInputBuffer(&bufferIndex);

            if (err != OK) {
                break;
            }

            mAvailEncoderInputIndices.push_back(bufferIndex);
        }

        feedEncoderInputBuffers();
    }

    for (;;) {
        size_t bufferIndex;
        size_t offset;
        size_t size;
        int64_t timeUs;
        uint32_t flags;
        native_handle_t* handle = NULL;
        err = mEncoder->dequeueOutputBuffer(
                &bufferIndex, &offset, &size, &timeUs, &flags);

        if (err != OK) {
            if (err == INFO_FORMAT_CHANGED) {
                continue;
            } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
                mEncoder->getOutputBuffers(&mEncoderOutputBuffers);
                continue;
            }

            if (err == -EAGAIN) {
                err = OK;
            }
            break;
        }

        if (flags & MediaCodec::BUFFER_FLAG_EOS) {
            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatEOS);
            notify->post();
        } else {
#if 0
            if (mIsVideo) {
                int32_t videoBitrate = GetInt32Property(
                        "media.wfd.video-bitrate", 5000000);

                setVideoBitrate(videoBitrate);
            }
#endif

            sp<ABuffer> buffer;
            sp<ABuffer> outbuf = mEncoderOutputBuffers.itemAt(bufferIndex);

            if (outbuf->meta()->findPointer("handle", (void**)&handle) &&
                    handle != NULL) {
                int32_t rangeLength, rangeOffset;
                CHECK(outbuf->meta()->findInt32("rangeOffset", &rangeOffset));
                CHECK(outbuf->meta()->findInt32("rangeLength", &rangeLength));
                outbuf->meta()->setPointer("handle", NULL);

                // MediaSender will post the following message when HDCP
                // is done, to release the output buffer back to encoder.
                sp<AMessage> notify(new AMessage(
                        kWhatReleaseOutputBuffer, id()));
                notify->setInt32("bufferIndex", bufferIndex);

#ifdef USES_WFD_SERVICE
                mPenddingOutputBufferCount++;
#endif
                buffer = new ABuffer(
                        rangeLength > (int32_t)size ? rangeLength : size);
                buffer->meta()->setPointer("handle", handle);
                buffer->meta()->setInt32("rangeOffset", rangeOffset);
                buffer->meta()->setInt32("rangeLength", rangeLength);
                buffer->meta()->setMessage("notify", notify);
            } else {
                buffer = new ABuffer(size);
            }

            buffer->meta()->setInt64("timeUs", timeUs);

            ALOGV("[%s] time %lld us (%.2f secs)",
                  mIsVideo ? "video" : "audio", timeUs, timeUs / 1E6);

            memcpy(buffer->data(), outbuf->base() + offset, size);

            if (flags & MediaCodec::BUFFER_FLAG_CODECCONFIG) {
#ifdef USES_WFD_SERVICE
                if (handle != NULL) {
                    void *addr = (void *)handle->data[1];
                    int size = handle->data[3];
                    sp<ABuffer> tempBuffer = new ABuffer(size);
                    memcpy(tempBuffer->data(), addr, size);
                    mOutputFormat->setBuffer("csd-0", tempBuffer);
                    mPenddingOutputBufferCount--;
                    mEncoder->releaseOutputBuffer(bufferIndex);
                }
#else
                if (!handle) {
                    if (mIsH264) {
                        mCSD0 = buffer;
                    }
                    mOutputFormat->setBuffer("csd-0", buffer);
                }
#endif
            } else {
                if (mNeedToManuallyPrependSPSPPS
                        && mIsH264
                        && (mFlags & FLAG_PREPEND_CSD_IF_NECESSARY)
                        && IsIDR(buffer)) {
                    buffer = prependCSD(buffer);
                }

                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatAccessUnit);
                notify->setBuffer("accessUnit", buffer);
                notify->post();
            }
        }

        if (!handle) {
            mEncoder->releaseOutputBuffer(bufferIndex);
        }

        if (flags & MediaCodec::BUFFER_FLAG_EOS) {
            break;
        }
    }

    return err;
}

void Converter::requestIDRFrame() {
    (new AMessage(kWhatRequestIDRFrame, id()))->post();
}

void Converter::dropAFrame() {
    // Unsupported in surface input mode.
    CHECK(!(mFlags & FLAG_USE_SURFACE_INPUT));

    (new AMessage(kWhatDropAFrame, id()))->post();
}

void Converter::suspendEncoding(bool suspend) {
    sp<AMessage> msg = new AMessage(kWhatSuspendEncoding, id());
    msg->setInt32("suspend", suspend);
    msg->post();
}

int32_t Converter::getVideoBitrate() const {
    return mPrevVideoBitrate;
}

void Converter::setVideoBitrate(int32_t bitRate) {
    if (mIsVideo && mEncoder != NULL && bitRate != mPrevVideoBitrate) {
        sp<AMessage> params = new AMessage;
        params->setInt32("video-bitrate", bitRate);

        mEncoder->setParameters(params);

        mPrevVideoBitrate = bitRate;
    }
}

#ifdef USES_WFD_SERVICE
status_t Converter::makeWFDServiceListener()
{
    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder = sm->getService(String16("media.wifi_display"));
    sp<IWFDService> wfdService = interface_cast<IWFDService>(binder);
    mWfdServiceListener = new WFDServiceListener(
        (void *)this, &callbackWFDService, WFD_ENCODER_CMD);
    return wfdService->addWFDServiceListener(mWfdServiceListener);
}

static unsigned int MeasureTime(struct timeval *start, struct timeval *stop)
{
    unsigned long sec, usec, time;

    sec = stop->tv_sec - start->tv_sec;
    if (stop->tv_usec >= start->tv_usec) {
        usec = stop->tv_usec - start->tv_usec;
    } else {
        usec = stop->tv_usec + 1000000 - start->tv_usec;
        sec--;
    }

    time = sec * 1000000 + (usec);

    return time;
}

void Converter::callbackWFDService(void *me, int commandType, int ext1, int ext2)
{
    Converter *converter = (Converter *)me;
    ALOGE("callbackWFDService(), commandType 0x%x, ext1 0x%x, ext2 0x%x", commandType, ext1, ext2);

    switch(commandType) {
    case WFD_ENCODER_CMD:
    {
        switch(ext1) {
        case WFD_ENCODER_EXT1_UNLOAD:
        {
            ALOGE("callbackWFDService() WFD_ENCODER_EXT1_UNLOAD, ext1 0x%x, ext2 0x%x", ext1, ext2);

            struct timeval start, stop;
            gettimeofday(&start, NULL);

            converter->internalStop();
            converter->internalShutdown();

            gettimeofday(&stop, NULL);

            ALOGE("callbackWFDService() WFD_ENCODER_EXT1_UNLOAD, elapsed time %d", MeasureTime(&start, &stop));

            break;
        }

        case WFD_ENCODER_EXT1_LOAD_NORMAL:
        {
            ALOGE("callbackWFDService(), WFD_ENCODER_EXT1_LOAD_NORMAL, ext1 0x%x, ext2 0x%x", ext1, ext2);

            converter->init(false);
            converter->requestIDRFrame();

            break;
        }

        case WFD_ENCODER_EXT1_LOAD_SECURE:
        {
            ALOGE("callbackWFDService(), WFD_ENCODER_EXT1_LOAD_SECURE, ext1 0x%x, ext2 0x%x", ext1, ext2);

            /* Todo: enable matic key setting
            if (MFCDRM_set_magic_key_hdcp() != MFCDRM_SUCCESS)
                ALOGE("MFCDRM_set_magic_key_hdcp FAIL");
            */

            converter->init(true);
            converter->requestIDRFrame();

            break;
        }

        default:
            ALOGE("Unknown ext1 in wfdServiceNotify");
        }
        break;
    }

    default:
        ALOGE("Unknown command in wfdServiceNotify");
        break;
    }
}
#endif

}  // namespace android
