LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

include frameworks/av/media/libstagefright/codecs/common/Config.mk

USE_AM_SOFT_DEMUXER_CODEC := true

LOCAL_SRC_FILES:=                         \
        ACodec.cpp                        \
        AACExtractor.cpp                  \
        ADIFExtractor.cpp                  \
        ADTSExtractor.cpp                  \
        LATMExtractor.cpp                  \
        THDExtractor.cpp                  \
        AACWriter.cpp                     \
        AMRExtractor.cpp                  \
        AMRWriter.cpp                     \
        AudioPlayer.cpp                   \
        AudioSource.cpp                   \
        AwesomePlayer.cpp                 \
        CameraSource.cpp                  \
        CameraSourceTimeLapse.cpp         \
        DataSource.cpp                    \
        DRMExtractor.cpp                  \
        ESDS.cpp                          \
        FileSource.cpp                    \
        FLACExtractor.cpp                 \
        HTTPBase.cpp                      \
        JPEGSource.cpp                    \
        MP3Extractor.cpp                  \
        MPEG2TSWriter.cpp                 \
        MPEG4Extractor.cpp                \
        MPEG4Writer.cpp                   \
        MediaAdapter.cpp                  \
        MediaBuffer.cpp                   \
        MediaBufferGroup.cpp              \
        MediaCodec.cpp                    \
        MediaCodecList.cpp                \
        MediaDefs.cpp                     \
        MediaExtractor.cpp                \
        MediaMuxer.cpp                    \
        MediaSource.cpp                   \
        MetaData.cpp                      \
        NuCachedSource2.cpp               \
        NuMediaExtractor.cpp              \
        OMXClient.cpp                     \
        OMXCodec.cpp                      \
        OggExtractor.cpp                  \
        SampleIterator.cpp                \
        SampleTable.cpp                   \
        SkipCutBuffer.cpp                 \
        StagefrightMediaScanner.cpp       \
        StagefrightMetadataRetriever.cpp  \
        SurfaceMediaSource.cpp            \
        ThrottledSource.cpp               \
        TimeSource.cpp                    \
        TimedEventQueue.cpp               \
        Utils.cpp                         \
        VBRISeeker.cpp                    \
        WAVExtractor.cpp                  \
        WVMExtractor.cpp                  \
        XINGSeeker.cpp                    \
        avc_utils.cpp                     \
        mp4/FragmentedMP4Parser.cpp       \
        mp4/TrackFragment.cpp             \
        AsfExtractor/ASFExtractor.cpp     \
	DtshdExtractor.cpp  \
        AIFFExtractor.cpp                 \


ifeq ($(BOARD_WIDEVINE_SUPPORTLEVEL),1)
LOCAL_CFLAGS += -DBOARD_WIDEVINE_SUPPORTLEVEL=1
else
LOCAL_CFLAGS += -DBOARD_WIDEVINE_SUPPORTLEVEL=3
endif

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/include/media/stagefright/timedtext \
        $(TOP)/frameworks/native/include/media/hardware \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/services/connectivitymanager \
        $(TOP)/external/flac/include \
        $(TOP)/external/tremolo \
        $(TOP)/external/openssl/include \
	$(LOCAL_PATH)/codecs/adif/include
	
ifeq ($(BUILD_WITH_AMLOGIC_PLAYER),true)
    AMPLAYER_APK_DIR=$(TOP)/packages/amlogic/LibPlayer/
    LOCAL_C_INCLUDES += \
        $(AMPLAYER_APK_DIR)/amplayer/player/include     \
        $(AMPLAYER_APK_DIR)/amplayer/control/include    \
        $(AMPLAYER_APK_DIR)/amadec/include              \
        $(AMPLAYER_APK_DIR)/amcodec/include             \
        $(AMPLAYER_APK_DIR)/amavutils/include           \
        $(AMPLAYER_APK_DIR)/amvdec/include           \
        $(AMPLAYER_APK_DIR)/amffmpeg/

   #LOCAL_SHARED_LIBRARIES += libui
   #LOCAL_SHARED_LIBRARIES += libamplayer libamavutils libamvdec
   #LOCAL_CFLAGS += -DBUILD_WITH_AMLOGIC_PLAYER=1
   #LOCAL_CFLAGS += -DLOCAL_OEMCRYPTO_LEVEL=$(LOCAL_OEMCRYPTO_LEVEL)
  # LOCAL_CFLAGS += -DLOCAL_OEMCRYPTO_LEVEL=$(BOARD_WIDEVINE_OEMCRYPTO_LEVEL)
endif

LOCAL_SHARED_LIBRARIES := \
        libbinder \
        libcamera_client \
        libconnectivitymanager \
        libcutils \
        libdl \
        libdrmframework \
        libexpat \
        libgui \
        libicui18n \
        libicuuc \
        liblog \
        libmedia \
        libsonivox \
        libssl \
        libstagefright_omx \
        libstagefright_yuv \
        libsync \
        libui \
        libutils \
        libvorbisidec \
        libz \
        libpowermanager

LOCAL_STATIC_LIBRARIES := \
        libstagefright_color_conversion \
        libstagefright_aacenc \
        libstagefright_matroska \
        libstagefright_timedtext \
        libvpx \
        libwebm \
        libstagefright_mpeg2ts \
        libstagefright_id3 \
        libFLAC \
        libmedia_helper \
        libstagefright_hevcutils \


LOCAL_SRC_FILES += \
        chromium_http_stub.cpp
LOCAL_CPPFLAGS += -DCHROMIUM_AVAILABLE=1

LOCAL_SHARED_LIBRARIES += libstlport
include external/stlport/libstlport.mk

LOCAL_SHARED_LIBRARIES += \
        libstagefright_enc_common \
        libstagefright_avc_common \
        libstagefright_foundation \
        libdl \
        libamffmpeg \

LOCAL_STATIC_LIBRARIES += \
	libstagefright_adifdec

LOCAL_SRC_FILES += \
	SStreamingExtractor.cpp
ifeq ($(BOARD_PLAYREADY_LP_IN_SS), true)
LOCAL_CFLAGS += -DSS_MSPLAYREADY_TEST
endif
LOCAL_C_INCLUDES+= \
	$(TOP)/frameworks/av/media/libstagefright/include  \
	$(TOP)/frameworks/av/media/libmediaplayerservice  \
	$(TOP)/packages/amlogic/LibPlayer/amffmpeg

LOCAL_CFLAGS += -Wno-multichar

ifeq ($(USE_AM_SOFT_DEMUXER_CODEC),true)
LOCAL_SRC_FILES += \
        AmMediaDefsExt.cpp                \
        AmMediaExtractorPlugin.cpp        \

LOCAL_CFLAGS += -DUSE_AM_SOFT_DEMUXER_CODEC
endif

LOCAL_MODULE:= libstagefright

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))
