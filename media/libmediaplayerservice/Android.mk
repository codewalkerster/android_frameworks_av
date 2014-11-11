LOCAL_PATH:= $(call my-dir)

#
# libmediaplayerservice
#

include $(CLEAR_VARS)

GRALLOC_DIR := hardware/amlogic/gralloc

LOCAL_SRC_FILES:=               \
    ActivityManager.cpp         \
    Crypto.cpp                  \
    Drm.cpp                     \
    HDCP.cpp                    \
    MediaPlayerFactory.cpp      \
    MediaPlayerService.cpp      \
    MediaRecorderClient.cpp     \
    MetadataRetrieverClient.cpp \
    MidiFile.cpp                \
    MidiMetadataRetriever.cpp   \
    RemoteDisplay.cpp           \
    SharedLibrary.cpp           \
    StagefrightPlayer.cpp       \
    StagefrightRecorder.cpp     \
    TestPlayerStub.cpp          \

ifeq ($(BOARD_WIDEVINE_SUPPORTLEVEL),1)
LOCAL_CFLAGS += -DBOARD_WIDEVINE_SUPPORTLEVEL=1
else
LOCAL_CFLAGS += -DBOARD_WIDEVINE_SUPPORTLEVEL=3
endif

ifeq ($(LIVEPLAY_SEEK), true)
 LOCAL_CFLAGS += -DLIVEPLAY_SEEK
endif

ifeq ($(BUILD_WITH_AMLOGIC_PLAYER),true)
    LOCAL_SRC_FILES +=                          \
        AmSuperPlayer.cpp                       \
        AmlogicPlayer.cpp                       \
        SubSource.cpp                       \
        AmlogicPlayerRender.cpp                 \
        AmlogicPlayerStreamSource.cpp           \
        AmlogicPlayerStreamSourceListener.cpp   \
        AmlogicPlayerExtractorDemux.cpp         \
        AmlogicPlayerExtractorDataSource.cpp    \
        AmlogicPlayerDataSouceProtocol.cpp      \
        AmlPlayerMetadataRetriever.cpp \
        AmlPlayerMetadataRetriever0.cpp 
        
endif

LOCAL_SHARED_LIBRARIES :=       \
    libbinder                   \
    libcamera_client            \
    libcutils                   \
    liblog                      \
    libdl                       \
    libgui                      \
    libmedia                    \
    libsonivox                  \
    libstagefright              \
    libstagefright_foundation   \
    libstagefright_httplive     \
    libstagefright_omx          \
    libstagefright_wfd          \
    libutils                    \
    libvorbisidec               \
    libdrmframework \
    libamthumbnail  \

LOCAL_STATIC_LIBRARIES :=       \
    libstagefright_nuplayer     \
    libstagefright_rtsp         \

LOCAL_C_INCLUDES :=                                                 \
    $(call include-path-for, graphics corecg)                       \
    $(TOP)/frameworks/av/media/libstagefright/include               \
    $(TOP)/frameworks/av/media/libstagefright/rtsp                  \
    $(TOP)/frameworks/av/media/libstagefright/wifi-display          \
    $(TOP)/frameworks/native/include/media/openmax                  \
    $(TOP)/external/tremolo/Tremolo                                 \
    $(GRALLOC_DIR) \

ifeq ($(BOARD_PLAYREADY_TVP),true)
LOCAL_CFLAGS += -DBOARD_PLAYREADY_TVP
endif
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

   LOCAL_SHARED_LIBRARIES += libui
   LOCAL_SHARED_LIBRARIES += libamplayer libamavutils libamvdec
   LOCAL_CFLAGS += -DBUILD_WITH_AMLOGIC_PLAYER=1
endif

LOCAL_MODULE:= libmediaplayerservice

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))