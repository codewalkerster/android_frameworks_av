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
#define LOG_TAG "PlaybackSession"
#include <utils/Log.h>

#include "PlaybackSession.h"

#include "Converter.h"
#include "MediaPuller.h"
#include "RepeaterSource.h"
#include "include/avc_utils.h"
#include "VdinMediaSource.h"
#include "WifiDisplaySource.h"

#include <binder/IServiceManager.h>
#include <cutils/properties.h>
#include <media/IHDCP.h>
#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/AudioSource.h>
//  include frameworks/av/include/media/AudioRecord.h
//      include frameworks/av/include/media/AudioSystem.h
//          system/core/include/system/audio.h

#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <media/stagefright/SurfaceMediaSource.h>
#include <media/stagefright/Utils.h>

#include <OMX_IVCommon.h>

#include <media/AudioSystem.h>   //for audio policy

namespace android {

struct WifiDisplaySource::PlaybackSession::Track : public AHandler {
    enum {
        kWhatStopped,
    };

    Track(const sp<AMessage> &notify,
          const sp<ALooper> &pullLooper,
          const sp<ALooper> &codecLooper,
          const sp<MediaPuller> &mediaPuller,
          const sp<Converter> &converter);

    Track(const sp<AMessage> &notify, const sp<AMessage> &format);

    void setRepeaterSource(const sp<RepeaterSource> &source);

    sp<AMessage> getFormat();
    bool isAudio() const;

    const sp<Converter> &converter() const;
    const sp<RepeaterSource> &repeaterSource() const;

    ssize_t mediaSenderTrackIndex() const;
    void setMediaSenderTrackIndex(size_t index);

    status_t start();
    void stopAsync();

    void pause();
    void resume();

    void queueAccessUnit(const sp<ABuffer> &accessUnit);
    sp<ABuffer> dequeueAccessUnit();

    bool hasOutputBuffer(int64_t *timeUs) const;
    void queueOutputBuffer(const sp<ABuffer> &accessUnit);
    sp<ABuffer> dequeueOutputBuffer();

#if SUSPEND_VIDEO_IF_IDLE
    bool isSuspended() const;
#endif

    size_t countQueuedOutputBuffers() const {
        return mQueuedOutputBuffers.size();
    }

    void requestIDRFrame();

protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);
    virtual ~Track();

private:
    enum {
        kWhatMediaPullerStopped,
    };

    sp<AMessage> mNotify;
    sp<ALooper> mPullLooper;
    sp<ALooper> mCodecLooper;
    sp<MediaPuller> mMediaPuller;
    sp<Converter> mConverter;
    sp<AMessage> mFormat;
    bool mStarted;
    ssize_t mMediaSenderTrackIndex;
    bool mIsAudio;
    List<sp<ABuffer> > mQueuedAccessUnits;
    sp<RepeaterSource> mRepeaterSource;
    List<sp<ABuffer> > mQueuedOutputBuffers;
    int64_t mLastOutputBufferQueuedTimeUs;

    static bool IsAudioFormat(const sp<AMessage> &format);

    DISALLOW_EVIL_CONSTRUCTORS(Track);
};

WifiDisplaySource::PlaybackSession::Track::Track(
        const sp<AMessage> &notify,
        const sp<ALooper> &pullLooper,
        const sp<ALooper> &codecLooper,
        const sp<MediaPuller> &mediaPuller,
        const sp<Converter> &converter)
    : mNotify(notify),
      mPullLooper(pullLooper),
      mCodecLooper(codecLooper),
      mMediaPuller(mediaPuller),
      mConverter(converter),
      mStarted(false),
      mIsAudio(IsAudioFormat(mConverter->getOutputFormat())),
      mLastOutputBufferQueuedTimeUs(-1ll) {
}

WifiDisplaySource::PlaybackSession::Track::Track(
        const sp<AMessage> &notify, const sp<AMessage> &format)
    : mNotify(notify),
      mFormat(format),
      mStarted(false),
      mIsAudio(IsAudioFormat(format)),
      mLastOutputBufferQueuedTimeUs(-1ll) {
}

WifiDisplaySource::PlaybackSession::Track::~Track() {
    CHECK(!mStarted);
}

// static
bool WifiDisplaySource::PlaybackSession::Track::IsAudioFormat(
        const sp<AMessage> &format) {
    AString mime;
    CHECK(format->findString("mime", &mime));

    return !strncasecmp(mime.c_str(), "audio/", 6);
}

sp<AMessage> WifiDisplaySource::PlaybackSession::Track::getFormat() {
    return mFormat != NULL ? mFormat : mConverter->getOutputFormat();
}

bool WifiDisplaySource::PlaybackSession::Track::isAudio() const {
    return mIsAudio;
}

const sp<Converter> &WifiDisplaySource::PlaybackSession::Track::converter() const {
    return mConverter;
}

const sp<RepeaterSource> &
WifiDisplaySource::PlaybackSession::Track::repeaterSource() const {
    return mRepeaterSource;
}

ssize_t WifiDisplaySource::PlaybackSession::Track::mediaSenderTrackIndex() const {
    CHECK_GE(mMediaSenderTrackIndex, 0);
    return mMediaSenderTrackIndex;
}

void WifiDisplaySource::PlaybackSession::Track::setMediaSenderTrackIndex(
        size_t index) {
    mMediaSenderTrackIndex = index;
}

status_t WifiDisplaySource::PlaybackSession::Track::start() {
    ALOGI("Track::start isAudio=%d", mIsAudio);

    CHECK(!mStarted);

    status_t err = OK;

    if (mMediaPuller != NULL) {
        err = mMediaPuller->start();
    }

    if (err == OK) {
        mStarted = true;
    }

    return err;
}

void WifiDisplaySource::PlaybackSession::Track::stopAsync() {
    ALOGI("Track::stopAsync isAudio=%d", mIsAudio);

    if (mConverter != NULL) {
        mConverter->shutdownAsync();
    }

    sp<AMessage> msg = new AMessage(kWhatMediaPullerStopped, id());

    if (mStarted && mMediaPuller != NULL) {
        if (mRepeaterSource != NULL) {
            // Let's unblock MediaPuller's MediaSource::read().
            mRepeaterSource->wakeUp();
        }

        mMediaPuller->stopAsync(msg);
    } else {
        mStarted = false;
        msg->post();
    }
}

void WifiDisplaySource::PlaybackSession::Track::pause() {
    mMediaPuller->pause();
}

void WifiDisplaySource::PlaybackSession::Track::resume() {
    mMediaPuller->resume();
}

void WifiDisplaySource::PlaybackSession::Track::onMessageReceived(
        const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatMediaPullerStopped:
        {
            mConverter.clear();

            mStarted = false;

            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatStopped);
            notify->post();

            ALOGI("kWhatStopped %s posted", mIsAudio ? "audio" : "video");
            break;
        }

        default:
            TRESPASS();
    }
}

void WifiDisplaySource::PlaybackSession::Track::queueAccessUnit(
        const sp<ABuffer> &accessUnit) {
    mQueuedAccessUnits.push_back(accessUnit);
}

sp<ABuffer> WifiDisplaySource::PlaybackSession::Track::dequeueAccessUnit() {
    if (mQueuedAccessUnits.empty()) {
        return NULL;
    }

    sp<ABuffer> accessUnit = *mQueuedAccessUnits.begin();
    CHECK(accessUnit != NULL);

    mQueuedAccessUnits.erase(mQueuedAccessUnits.begin());

    return accessUnit;
}

void WifiDisplaySource::PlaybackSession::Track::setRepeaterSource(
        const sp<RepeaterSource> &source) {
    mRepeaterSource = source;
}

void WifiDisplaySource::PlaybackSession::Track::requestIDRFrame() {
    if (mIsAudio) {
        return;
    }

    if (mRepeaterSource != NULL) {
        mRepeaterSource->wakeUp();
    }

    mConverter->requestIDRFrame();
}

bool WifiDisplaySource::PlaybackSession::Track::hasOutputBuffer(
        int64_t *timeUs) const {
    *timeUs = 0ll;

    if (mQueuedOutputBuffers.empty()) {
        return false;
    }

    const sp<ABuffer> &outputBuffer = *mQueuedOutputBuffers.begin();

    CHECK(outputBuffer->meta()->findInt64("timeUs", timeUs));

    return true;
}

void WifiDisplaySource::PlaybackSession::Track::queueOutputBuffer(
        const sp<ABuffer> &accessUnit) {
    mQueuedOutputBuffers.push_back(accessUnit);
    mLastOutputBufferQueuedTimeUs = ALooper::GetNowUs();
}

sp<ABuffer> WifiDisplaySource::PlaybackSession::Track::dequeueOutputBuffer() {
    CHECK(!mQueuedOutputBuffers.empty());

    sp<ABuffer> outputBuffer = *mQueuedOutputBuffers.begin();
    mQueuedOutputBuffers.erase(mQueuedOutputBuffers.begin());

    return outputBuffer;
}

#if SUSPEND_VIDEO_IF_IDLE
bool WifiDisplaySource::PlaybackSession::Track::isSuspended() const {
    if (!mQueuedOutputBuffers.empty()) {
        return false;
    }

    if (mLastOutputBufferQueuedTimeUs < 0ll) {
        // We've never seen an output buffer queued, but tracks start
        // out live, not suspended.
        return false;
    }

    // If we've not seen new output data for 60ms or more, we consider
    // this track suspended for the time being.
    return (ALooper::GetNowUs() - mLastOutputBufferQueuedTimeUs) > 60000ll;
}
#endif

////////////////////////////////////////////////////////////////////////////////
char* freadline(FILE *stream)
{
    static char a_line[500];
    int count = 0;

    while((count < 500) && ((a_line[count++] = getc(stream)) != '\n'));

    a_line[count - 1] = '\0';

    return a_line;
}
WifiDisplaySource::PlaybackSession::PlaybackSession(
        const sp<ANetworkSession> &netSession,
        const sp<AMessage> &notify,
        const in_addr &interfaceAddr,
        const sp<IHDCP> &hdcp,
        const char *path)
    : mNetSession(netSession),
      mNotify(notify),
      mInterfaceAddr(interfaceAddr),
      mHDCP(hdcp),
      mLocalRTPPort(-1),
      mWeAreDead(false),
      mPaused(false),
      mLastLifesignUs(),
      mVideoTrackIndex(-1),
      mPrevTimeUs(-1ll),
      mAllTracksHavePacketizerIndex(false),
      mDebug(0l),
      mDebugFileName("") {
    

    FILE* dataFile;
    char *str,*strData;
  
    dataFile=fopen("/system/etc/wfd_source.conf","rb");
    if(NULL==dataFile)
    {
        ALOGI("========we cant open file wfd_source.conf===========");
        return ;
    }
    while(!feof(dataFile)){
    str=freadline(dataFile);
    strData=strchr(str,'=');
    if(strData) {
        *strData='\0';
        strData++;
    }
    else break;
    if(strcmp(str,"debug")==0){
	mDebug=strtoul(strData,NULL,0);
        ALOGI("%s : %d",str,mDebug);
    }
    if(strcmp(str,"source")==0){
	mDebugFileName.setTo(strData);
	ALOGI("source file:%s",mDebugFileName.c_str());	
    } 
    }
    fclose(dataFile);
}

#if 0
status_t WifiDisplaySource::PlaybackSession::init(
        const char *clientIP, int32_t clientRtp, int32_t clientRtcp,
        Sender::TransportMode transportMode,
        bool usePCMAudio) {
    ALOGI("%s %d", __FUNCTION__, __LINE__);
    status_t err = setupPacketizer(usePCMAudio);
    if (err != OK) {
        ALOGI("%s %d, err = %d", __FUNCTION__, __LINE__, err);
        return err;
    }
    ALOGI("%s %d", __FUNCTION__, __LINE__);
    sp<AMessage> notify = new AMessage(kWhatSenderNotify, id());
    mSender = new Sender(mNetSession, notify);

    mSenderLooper = new ALooper;
    mSenderLooper->setName("sender_looper");

    mSenderLooper->start(
            false /* runOnCallingThread */,
            false /* canCallJava */,
            PRIORITY_AUDIO);

    mSenderLooper->registerHandler(mSender);

    ALOGI("mSender->init >>>");
    err = mSender->init(clientIP, clientRtp, clientRtcp, transportMode);
    ALOGI("mSender->init <<<");
    if (err != OK) {
        ALOGI("%s %d, err = %d", __FUNCTION__, __LINE__, err);
        return err;
    }

    updateLiveness();

    return OK;
}
#endif

status_t WifiDisplaySource::PlaybackSession::init(
        const char *clientIP,
        int32_t clientRtp,
        RTPSender::TransportMode rtpMode,
        int32_t clientRtcp,
        RTPSender::TransportMode rtcpMode,
        bool enableAudio,
        bool usePCMAudio,
        bool enableVideo,
        VideoFormats::ResolutionType videoResolutionType,
        size_t videoResolutionIndex,
        VideoFormats::ProfileType videoProfileType,
        VideoFormats::LevelType videoLevelType) {
    sp<AMessage> notify = new AMessage(kWhatMediaSenderNotify, id());
    mMediaSender = new MediaSender(mNetSession, notify);
    looper()->registerHandler(mMediaSender);

    mMediaSender->setHDCP(mHDCP);

	AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_IN_REMOTE_SUBMIX, AUDIO_POLICY_DEVICE_STATE_AVAILABLE, 0);
	AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_REMOTE_SUBMIX, AUDIO_POLICY_DEVICE_STATE_AVAILABLE, 0);

    status_t err = setupPacketizer(
            enableAudio,
            usePCMAudio,
            enableVideo,
            videoResolutionType,
            videoResolutionIndex,
            videoProfileType,
            videoLevelType);

    if (err == OK) {
        err = mMediaSender->initAsync(
                -1 ,//trackIndex
                clientIP,
                clientRtp,
                rtpMode,
                clientRtcp,
                rtcpMode,
                &mLocalRTPPort);
    }

    if (err != OK) {
        mLocalRTPPort = -1;

        looper()->unregisterHandler(mMediaSender->id());
        mMediaSender.clear();
        return err;
    }

    updateLiveness();

    return OK;
}

	
WifiDisplaySource::PlaybackSession::~PlaybackSession() {
}

int32_t WifiDisplaySource::PlaybackSession::getRTPPort() const {
    return mLocalRTPPort;
}

int64_t WifiDisplaySource::PlaybackSession::getLastLifesignUs() const {
    return mLastLifesignUs;
}

void WifiDisplaySource::PlaybackSession::updateLiveness() {
    mLastLifesignUs = ALooper::GetNowUs();
}

status_t WifiDisplaySource::PlaybackSession::play() {
    updateLiveness();

    (new AMessage(kWhatResume, id()))->post();

    return OK;
}

status_t WifiDisplaySource::PlaybackSession::finishPlay() {
    // XXX Give the dongle a second to bind its sockets.
    //(new AMessage(kWhatFinishPlay, id()))->post(1000000ll);
    return OK;
}

////////////////////////////////////////////////////////////////////////////////
#if 0
struct OutputThread : public Thread {
    OutputThread(uint32_t notifyID,ALooper::handler_id id,AString &fileName,sp<Sender>);

protected:
    virtual ~OutputThread();

    virtual bool threadLoop();

private:


    ALooper::handler_id mCallerID;
    AString mFileName;
    uint32_t mNotifyID;
    bool mSelfOutput;
    sp<Sender> mSender; 

    OutputThread(const OutputThread &);
    OutputThread &operator=(const OutputThread &);
};

OutputThread::OutputThread(uint32_t notifyID,
       ALooper::handler_id id,AString &filename,sp<Sender> sender)
    : mCallerID(id),
      mFileName(filename),
      mNotifyID(notifyID),
      mSelfOutput(true),
      mSender(sender) 
{
	
}

OutputThread::~OutputThread() {
   
}
//do our seperate work here.
bool OutputThread::threadLoop() {
    	status_t err;
    	size_t  bufferSize=188*10;
    	FILE* mDataFile;
    	//we open file first.
	if(strcmp(mFileName.c_str(),"")==0 ) return false;
    	mDataFile=fopen(mFileName.c_str(),"rb"); 
	if(NULL==mDataFile) 
	{
		ALOGI("========we cant open source TS file %s===========",mFileName.c_str());
		return false;
	}
	ALOGI("========MM start read source TS file===========");
	while(mSelfOutput) {
        size_t bufferIndex;
        size_t offset;
        size_t size;
        int64_t timeUs;
        uint32_t flags;


        
        sp<ABuffer> buffer = new ABuffer(bufferSize);
	timeUs=ALooper::GetNowUs();
        buffer->meta()->setInt64("timeUs", timeUs);
       // ALOGI("[%s] time %lld us (%.2f secs)",
       //           "video", timeUs, timeUs / 1E6);
	//we input file and buffer here.
	fread(buffer->data(),1,bufferSize,mDataFile); 
	if(feof(mDataFile)) fseek(mDataFile,0,SEEK_SET);
	
	mSender->queuePackets(timeUs, buffer);
	//sp<AMessage> notify; 
	//notify = new AMessage(mNotifyID,mCallerID);
 	//notify->setInt32("what", Converter::kWhatAccessUnit);
    	//notify->setBuffer("accessUnit", buffer);
	//notify->setSize("trackIndex",0);
    	//notify->post();
	}
	fclose(mDataFile);
    
    return true;
}
#endif

status_t WifiDisplaySource::PlaybackSession::onMediaSenderInitialized() {
    for (size_t i = 0; i < mTracks.size(); ++i) {
        CHECK_EQ((status_t)OK, mTracks.editValueAt(i)->start());
    }

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatSessionEstablished);
    notify->post();
   
    ALOGI("========receive connecttion info===========");
    static sp<Thread> threadOutput;
    if(mDebug)
    { 
	if(threadOutput != NULL) 
	{	
	   threadOutput->requestExitAndWait();
	}
    	//threadOutput=new OutputThread(kWhatConverterNotify,id(),mDebugFileName,mSender);
    	//threadOutput->run("wfd auto output thread");
    }
    return OK;
}

status_t WifiDisplaySource::PlaybackSession::pause() {
    updateLiveness();

    (new AMessage(kWhatPause, id()))->post();

    return OK;
}

void WifiDisplaySource::PlaybackSession::destroyAsync() {
    ALOGI("destroyAsync");

	AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_IN_REMOTE_SUBMIX, AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, 0);
	AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_REMOTE_SUBMIX, AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, 0);

    for (size_t i = 0; i < mTracks.size(); ++i) {
        mTracks.valueAt(i)->stopAsync();
    }
}

void WifiDisplaySource::PlaybackSession::onMessageReceived(
        const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatConverterNotify:
        {
            if (mWeAreDead) {
                ALOGI("dropping msg '%s' because we're dead",
                      msg->debugString().c_str());

                break;
            }

            int32_t what;
            CHECK(msg->findInt32("what", &what));

            size_t trackIndex;
            CHECK(msg->findSize("trackIndex", &trackIndex));

            if (what == Converter::kWhatAccessUnit) {
                sp<ABuffer> accessUnit;
                CHECK(msg->findBuffer("accessUnit", &accessUnit));

                const sp<Track> &track = mTracks.valueFor(trackIndex);

                status_t err = mMediaSender->queueAccessUnit(
                        //track->mediaSenderTrackIndex(),
                        trackIndex,
                        accessUnit);

                if (err != OK) {
                    notifySessionDead();
                }
                break;
            } else if (what == Converter::kWhatEOS) {
                CHECK_EQ(what, Converter::kWhatEOS);

                ALOGI("output EOS on track %d", trackIndex);

                ssize_t index = mTracks.indexOfKey(trackIndex);
                CHECK_GE(index, 0);

                const sp<Converter> &converter =
                    mTracks.valueAt(index)->converter();
                looper()->unregisterHandler(converter->id());

                mTracks.removeItemsAt(index);

                if (mTracks.isEmpty()) {
                    ALOGI("Reached EOS");
                }
            } else if (what != Converter::kWhatShutdownCompleted) {
                CHECK_EQ(what, Converter::kWhatError);

                status_t err;
                CHECK(msg->findInt32("err", &err));

                ALOGE("converter signaled error %d", err);

                notifySessionDead();
            }
            break;
        }

        case kWhatMediaSenderNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == MediaSender::kWhatInitDone) {
                status_t err;
                CHECK(msg->findInt32("err", &err));

                if (err == OK) {
                    onMediaSenderInitialized();
                } else {
                    notifySessionDead();
                }
            } else if (what == MediaSender::kWhatError) {
                notifySessionDead();
            } else if (what == MediaSender::kWhatNetworkStall) {
                size_t numBytesQueued;
                CHECK(msg->findSize("numBytesQueued", &numBytesQueued));

                if (mVideoTrackIndex >= 0) {
                    const sp<Track> &videoTrack =
                        mTracks.valueFor(mVideoTrackIndex);

                    sp<Converter> converter = videoTrack->converter();
                    if (converter != NULL) {
                        converter->dropAFrame();
                    }
                }
            } else if (what == MediaSender::kWhatInformSender) {
                onSinkFeedback(msg);
            } else {
                TRESPASS();
            }
            break;
        }

        case kWhatTrackNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            size_t trackIndex;
            CHECK(msg->findSize("trackIndex", &trackIndex));

            if (what == Track::kWhatStopped) {
                ALOGI("Track %d stopped", trackIndex);

                sp<Track> track = mTracks.valueFor(trackIndex);
                looper()->unregisterHandler(track->id());
                mTracks.removeItem(trackIndex);
                track.clear();

                if (!mTracks.isEmpty()) {
                    ALOGI("not all tracks are stopped yet");
                    break;
                }

                looper()->unregisterHandler(mMediaSender->id());
                mMediaSender.clear();

                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatSessionDestroyed);
                notify->post();
            }
            break;
        }

        case kWhatPause:
        {
            if (mExtractor != NULL) {
                ++mPullExtractorGeneration;
                mFirstSampleTimeRealUs = -1ll;
                mFirstSampleTimeUs = -1ll;
            }

            if (mPaused) {
                break;
            }

            for (size_t i = 0; i < mTracks.size(); ++i) {
                mTracks.editValueAt(i)->pause();
            }

            mPaused = true;
            break;
        }

        case kWhatResume:
        {
            if (mExtractor != NULL) {
                schedulePullExtractor();
            }

            if (!mPaused) {
                break;
            }

            for (size_t i = 0; i < mTracks.size(); ++i) {
                mTracks.editValueAt(i)->resume();
            }

            mPaused = false;
            break;
        }

        case kWhatPullExtractorSample:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));

            if (generation != mPullExtractorGeneration) {
                break;
            }

            mPullExtractorPending = false;

            onPullExtractor();
            break;
        }

        default:
            TRESPASS();
    }
}

void WifiDisplaySource::PlaybackSession::onSinkFeedback(const sp<AMessage> &msg) {
    int64_t avgLatencyUs;
    CHECK(msg->findInt64("avgLatencyUs", &avgLatencyUs));

    int64_t maxLatencyUs;
    CHECK(msg->findInt64("maxLatencyUs", &maxLatencyUs));

    ALOGI("sink reports avg. latency of %lld ms (max %lld ms)",
          avgLatencyUs / 1000ll,
          maxLatencyUs / 1000ll);

    if (mVideoTrackIndex >= 0) {
        const sp<Track> &videoTrack = mTracks.valueFor(mVideoTrackIndex);
        sp<Converter> converter = videoTrack->converter();

        if (converter != NULL) {
            int32_t videoBitrate =
                Converter::GetInt32Property("media.wfd.video-bitrate", -1);

            char val[PROPERTY_VALUE_MAX];
            if (videoBitrate < 0
                    && property_get("media.wfd.video-bitrate", val, NULL)
                    && !strcasecmp("adaptive", val)) {
                videoBitrate = converter->getVideoBitrate();

                if (avgLatencyUs > 300000ll) {
                    videoBitrate *= 0.6;
                } else if (avgLatencyUs < 100000ll) {
                    videoBitrate *= 1.1;
                }
            }

            if (videoBitrate > 0) {
                if (videoBitrate < 500000) {
                    videoBitrate = 500000;
                } else if (videoBitrate > 10000000) {
                    videoBitrate = 10000000;
                }

                if (videoBitrate != converter->getVideoBitrate()) {
                    ALOGI("setting video bitrate to %d bps", videoBitrate);

                    converter->setVideoBitrate(videoBitrate);
                }
            }
        }

        sp<RepeaterSource> repeaterSource = videoTrack->repeaterSource();
        if (repeaterSource != NULL) {
            double rateHz =
                Converter::GetInt32Property(
                        "media.wfd.video-framerate", -1);

            char val[PROPERTY_VALUE_MAX];
            if (rateHz < 0.0
                    && property_get("media.wfd.video-framerate", val, NULL)
                    && !strcasecmp("adaptive", val)) {
                 rateHz = repeaterSource->getFrameRate();

                if (avgLatencyUs > 300000ll) {
                    rateHz *= 0.9;
                } else if (avgLatencyUs < 200000ll) {
                    rateHz *= 1.1;
                }
            }

            if (rateHz > 0) {
                if (rateHz < 5.0) {
                    rateHz = 5.0;
                } else if (rateHz > 30.0) {
                    rateHz = 30.0;
                }

                if (rateHz != repeaterSource->getFrameRate()) {
                    ALOGI("setting frame rate to %.2f Hz", rateHz);

                    repeaterSource->setFrameRate(rateHz);
                }
            }
        }
    }
}

status_t WifiDisplaySource::PlaybackSession::setupMediaPacketizer(
        bool enableAudio, bool enableVideo) {
    DataSource::RegisterDefaultSniffers();

    addVideoSource();
    ALOGI("%s %d", __FUNCTION__, __LINE__);
    mExtractor = new NuMediaExtractor;

    status_t err = mExtractor->setDataSource(mMediaPath.c_str());

    if (err != OK) {
        return err;
    }

    ALOGI("%s %d", __FUNCTION__, __LINE__);
	
    size_t n = mExtractor->countTracks();
    bool haveAudio = false;
    bool haveVideo = false;
    for (size_t i = 0; i < n; ++i) {
        sp<AMessage> format;
        err = mExtractor->getTrackFormat(i, &format);

        if (err != OK) {
            continue;
        }

        AString mime;
        CHECK(format->findString("mime", &mime));

        bool isAudio = !strncasecmp(mime.c_str(), "audio/", 6);
        bool isVideo = !strncasecmp(mime.c_str(), "video/", 6);

        if (isAudio && enableAudio && !haveAudio) {
            haveAudio = true;
        } else if (isVideo && enableVideo && !haveVideo) {
            haveVideo = true;
        } else {
            continue;
        }

        err = mExtractor->selectTrack(i);

        size_t trackIndex = mTracks.size();

        sp<AMessage> notify = new AMessage(kWhatTrackNotify, id());
        notify->setSize("trackIndex", trackIndex);

        sp<Track> track = new Track(notify, format);
        looper()->registerHandler(track);

        mTracks.add(trackIndex, track);

        mExtractorTrackToInternalTrack.add(i, trackIndex);

        if (isVideo) {
            mVideoTrackIndex = trackIndex;
        }

        uint32_t flags = MediaSender::FLAG_MANUALLY_PREPEND_SPS_PPS;

        ssize_t mediaSenderTrackIndex =
            mMediaSender->addTrack(format, flags);
        CHECK_GE(mediaSenderTrackIndex, 0);

        track->setMediaSenderTrackIndex(mediaSenderTrackIndex);

        if ((haveAudio || !enableAudio) && (haveVideo || !enableVideo)) {
            break;
        }
    }

    return OK;
}

void WifiDisplaySource::PlaybackSession::schedulePullExtractor() {
    if (mPullExtractorPending) {
        return;
    }

    int64_t sampleTimeUs;
    status_t err = mExtractor->getSampleTime(&sampleTimeUs);

    int64_t nowUs = ALooper::GetNowUs();

    if (mFirstSampleTimeRealUs < 0ll) {
        mFirstSampleTimeRealUs = nowUs;
        mFirstSampleTimeUs = sampleTimeUs;
    }

    int64_t whenUs = sampleTimeUs - mFirstSampleTimeUs + mFirstSampleTimeRealUs;

    sp<AMessage> msg = new AMessage(kWhatPullExtractorSample, id());
    msg->setInt32("generation", mPullExtractorGeneration);
    msg->post(whenUs - nowUs);

    mPullExtractorPending = true;
}

void WifiDisplaySource::PlaybackSession::onPullExtractor() {
    sp<ABuffer> accessUnit = new ABuffer(1024 * 1024);
    status_t err = mExtractor->readSampleData(accessUnit);
    if (err != OK) {
        // EOS.
        return;
    }

    int64_t timeUs;
    CHECK_EQ((status_t)OK, mExtractor->getSampleTime(&timeUs));

    accessUnit->meta()->setInt64(
            "timeUs", mFirstSampleTimeRealUs + timeUs - mFirstSampleTimeUs);

    size_t trackIndex;
    CHECK_EQ((status_t)OK, mExtractor->getSampleTrackIndex(&trackIndex));

    sp<AMessage> msg = new AMessage(kWhatConverterNotify, id());

    msg->setSize(
            "trackIndex", mExtractorTrackToInternalTrack.valueFor(trackIndex));

    msg->setInt32("what", Converter::kWhatAccessUnit);
    msg->setBuffer("accessUnit", accessUnit);
    msg->post();

    mExtractor->advance();

    schedulePullExtractor();
}

status_t WifiDisplaySource::PlaybackSession::setupPacketizer(
        bool enableAudio,
        bool usePCMAudio,
        bool enableVideo,
        VideoFormats::ResolutionType videoResolutionType,
        size_t videoResolutionIndex,
        VideoFormats::ProfileType videoProfileType,
        VideoFormats::LevelType videoLevelType) {
    CHECK(enableAudio || enableVideo);

    if (!mMediaPath.empty()) {
        return setupMediaPacketizer(enableAudio, enableVideo);
    }

    if (enableVideo) {
        status_t err = addVideoSource(
                videoResolutionType, videoResolutionIndex, videoProfileType,
                videoLevelType);

        if (err != OK) {
            return err;
        }
    }

    if (!enableAudio) {
        return OK;
    }

    char val[256];
    if (property_get("media.wfd.disableAudio", val, NULL) && (!strcasecmp("true", val) || !strcmp("1", val))) {
        ALOGD("disableAudio");
        return OK;
    }

    return addAudioSource(usePCMAudio);
}

status_t WifiDisplaySource::PlaybackSession::addSource(
        bool isVideo, const sp<MediaSource> &source, bool isRepeaterSource,
        bool usePCMAudio, unsigned profileIdc, unsigned levelIdc,
        unsigned constraintSet, size_t *numInputBuffers) {
    CHECK(!usePCMAudio || !isVideo);
    CHECK(!isRepeaterSource || isVideo);
    CHECK(!profileIdc || isVideo);
    CHECK(!levelIdc || isVideo);
    CHECK(!constraintSet || isVideo);

    sp<ALooper> pullLooper = new ALooper;
    pullLooper->setName("pull_looper");

    pullLooper->start(
            false /* runOnCallingThread */,
            false /* canCallJava */,
            PRIORITY_AUDIO);

    sp<ALooper> codecLooper = new ALooper;
    codecLooper->setName("codec_looper");

    codecLooper->start(
            false /* runOnCallingThread */,
            false /* canCallJava */,
            PRIORITY_AUDIO);

    size_t trackIndex;

    sp<AMessage> notify;

    trackIndex = mTracks.size();

    sp<AMessage> format;
    ALOGI("%s %d", __FUNCTION__, __LINE__);

    status_t err = convertMetaDataToMessage(source->getFormat(), &format);
    CHECK_EQ(err, (status_t)OK);

    if (isVideo) {
        format->setString("mime", MEDIA_MIMETYPE_VIDEO_AVC);
        format->setInt32("store-metadata-in-buffers", true);
        format->setInt32("store-metadata-in-buffers-output", (mHDCP != NULL)
                && (mHDCP->getCaps() & HDCPModule::HDCP_CAPS_ENCRYPT_NATIVE));
        format->setInt32(
                "color-format", OMX_COLOR_FormatAndroidOpaque);
        format->setInt32("profile-idc", profileIdc);
        format->setInt32("level-idc", levelIdc);
        format->setInt32("constraint-set", constraintSet);
    } else {
        format->setString(
                "mime",
                usePCMAudio
                    ? MEDIA_MIMETYPE_AUDIO_RAW : MEDIA_MIMETYPE_AUDIO_AAC);
    }

    notify = new AMessage(kWhatConverterNotify, id());
    notify->setSize("trackIndex", trackIndex);

    sp<Converter> converter = new Converter(notify, codecLooper, format);

    looper()->registerHandler(converter);

    err = converter->init();
    if (err != OK) {
        ALOGE("%s converter returned err %d", isVideo ? "video" : "audio", err);

        looper()->unregisterHandler(converter->id());
        return err;
    }

    notify = new AMessage(Converter::kWhatMediaPullerNotify, converter->id());
    notify->setSize("trackIndex", trackIndex);

    ALOGI("%s %d", __FUNCTION__, __LINE__);
    sp<MediaPuller> puller = new MediaPuller(source, notify);
    pullLooper->registerHandler(puller);

    if (numInputBuffers != NULL) {
        *numInputBuffers = converter->getInputBufferCount();
    }

    notify = new AMessage(kWhatTrackNotify, id());
    notify->setSize("trackIndex", trackIndex);

    sp<Track> track = new Track(
            notify, pullLooper, codecLooper, puller, converter);

    if (isRepeaterSource) {
        track->setRepeaterSource(static_cast<RepeaterSource *>(source.get()));
    }

    looper()->registerHandler(track);

    mTracks.add(trackIndex, track);

    if (isVideo) {
        mVideoTrackIndex = trackIndex;
    }

#if 1

	ALOGE("[%s %d] isVideo:%d, trackIndex:%d", __FUNCTION__, __LINE__, isVideo, trackIndex);

	sp<AMessage> converterformat;

	converterformat = converter->getOutputFormat();

	if(isVideo == 1)
	{
		AString outputMIME;
		outputMIME = MEDIA_MIMETYPE_VIDEO_AVC;
		converterformat->setString("mime", outputMIME.c_str());
		ALOGI("[%s %d] MEDIA_MIMETYPE_VIDEO_AVC", __FUNCTION__, __LINE__);
	}

	if(isVideo == 0 && usePCMAudio== 1)
	{
		AString outputMIME;
		outputMIME = MEDIA_MIMETYPE_AUDIO_RAW;
		converterformat->setString("mime", outputMIME.c_str());
		ALOGI("[%s %d] MEDIA_MIMETYPE_AUDIO_RAW", __FUNCTION__, __LINE__);
	}

	uint32_t flags = MediaSender::FLAG_MANUALLY_PREPEND_SPS_PPS;
	
	ssize_t mediaSenderTrackIndex =
		mMediaSender->addTrack(converterformat, flags);

#endif
    return OK;
}

status_t WifiDisplaySource::PlaybackSession::addVideoSource() {
    sp<VdinMediaSource> source = new VdinMediaSource(width(), height(), true);
	//sp<SurfaceMediaSource> source = new SurfaceMediaSource(width(), height());

    source->setUseAbsoluteTimestamps();

    ALOGI("%s %d", __FUNCTION__, __LINE__);
    sp<RepeaterSource> videoSource =
        new RepeaterSource(source, 30.0 /* rateHz */);

    size_t numInputBuffers;
#if 1
    //status_t err = addSource(
    //        true /* isVideo */, videoSource, true /* isRepeaterSource */,
    //        false /* usePCMAudio */, &numInputBuffers, source->isMetaDataStoredInVideoBuffers());
    status_t err = addSource(
                true /* isVideo */, videoSource, true /* isRepeaterSource */,
                false/* usePCMAudio */, 0 /* profileIdc */, 0 /* levelIdc */,
                0 /* constraintSet */, &numInputBuffers /* numInputBuffers */);
#else
    status_t err = addSource(
            true /* isVideo */, source, false /* isRepeaterSource */,
            false /* usePCMAudio */, &numInputBuffers, source->isMetaDataStoredInVideoBuffers());
#endif

    if (err != OK) {
        return err;
    }

    err = source->setMaxAcquiredBufferCount(numInputBuffers);
    CHECK_EQ(err, (status_t)OK);

    //mBufferQueue = source->getBufferQueue();

    return OK;
}

status_t WifiDisplaySource::PlaybackSession::addVideoSource(
        VideoFormats::ResolutionType videoResolutionType,
        size_t videoResolutionIndex,
        VideoFormats::ProfileType videoProfileType,
        VideoFormats::LevelType videoLevelType) {
    size_t width, height, framesPerSecond;
    bool interlaced;
    CHECK(VideoFormats::GetConfiguration(
                videoResolutionType,
                videoResolutionIndex,
                &width,
                &height,
                &framesPerSecond,
                &interlaced));
	
	ALOGI("[%s %d] width:%d height:%d", __FUNCTION__, __LINE__, width, height);

	sp<VdinMediaSource> source = new VdinMediaSource(width, height,true);

	mVdinMediaSource = source;

	int videoframerate = 30;
	
	char val[PROPERTY_VALUE_MAX];
	if(property_get("media.wfd.videoframerate", val, NULL))
	{
		sscanf(val,"%d",&videoframerate);
		source->setFrameRate(videoframerate);
		ALOGD("setFrameRate %d", videoframerate);
	}

    unsigned profileIdc, levelIdc, constraintSet;
    CHECK(VideoFormats::GetProfileLevel(
                videoProfileType,
                videoLevelType,
                &profileIdc,
                &levelIdc,
                &constraintSet));

    //sp<SurfaceMediaSource> source = new SurfaceMediaSource(width, height);

    source->setUseAbsoluteTimestamps();

    //sp<RepeaterSource> videoSource =
        //new RepeaterSource(source, framesPerSecond);


	size_t numInputBuffers;
	status_t err = addSource(
			true /* isVideo */, source, false /* isRepeaterSource */,
			false /* usePCMAudio */, profileIdc, levelIdc, constraintSet,
			&numInputBuffers);


    if (err != OK) {
        return err;
    }

    err = source->setMaxAcquiredBufferCount(numInputBuffers);
    CHECK_EQ(err, (status_t)OK);

    //mBufferQueue = source->getBufferQueue();

    return OK;
}

status_t WifiDisplaySource::PlaybackSession::addAudioSource(bool usePCMAudio) {
    sp<AudioSource> audioSource = new AudioSource(
            AUDIO_SOURCE_REMOTE_SUBMIX,
            48000 /* sampleRate */,
            2 /* channelCount */);

    if (audioSource->initCheck() == OK) {
        return addSource(
                false /* isVideo */, audioSource, false /* isRepeaterSource */,
                usePCMAudio, 0 /* profileIdc */, 0 /* levelIdc */,
                0 /* constraintSet */, NULL /* numInputBuffers */);
    }

    ALOGW("Unable to instantiate audio source");

    return OK;
}

sp<IGraphicBufferProducer> WifiDisplaySource::PlaybackSession::getSurfaceTexture() {
    return mBufferQueue;
}

int32_t WifiDisplaySource::PlaybackSession::width() const {
    return 1280;
}

int32_t WifiDisplaySource::PlaybackSession::height() const {
    return 720;
}

void WifiDisplaySource::PlaybackSession::requestIDRFrame() {
    for (size_t i = 0; i < mTracks.size(); ++i) {
        const sp<Track> &track = mTracks.valueAt(i);

        track->requestIDRFrame();
    }
}

void WifiDisplaySource::PlaybackSession::notifySessionDead() {
    // Inform WifiDisplaySource of our premature death (wish).
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatSessionDead);
    notify->post();

    mWeAreDead = true;
}

void WifiDisplaySource::PlaybackSession::setVideoRotation(int degree) {
	if(mVdinMediaSource != NULL)
		mVdinMediaSource->setVideoRotation(degree);
}


}  // namespace android

