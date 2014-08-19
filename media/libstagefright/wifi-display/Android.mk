LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        MediaSender.cpp                 \
        Parameters.cpp                  \
        rtp/RTPSender.cpp               \
        source/Converter.cpp            \
        source/MediaPuller.cpp          \
        source/PlaybackSession.cpp      \
        source/RepeaterSource.cpp       \
        source/TSPacketizer.cpp         \
        source/WifiDisplaySource.cpp    \
        VideoFormats.cpp                \

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/av/media/libstagefright/mpeg2ts \

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libcutils                       \
        liblog                          \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libui                           \
        libutils                        \

ifeq ($(BOARD_USES_WFD_SERVICE),true)
LOCAL_CFLAGS += -DUSES_WFD_SERVICE
endif

ifeq ($(BOARD_USES_WIFI_DISPLAY),true)
LOCAL_CFLAGS += -DUSES_WIFI_DISPLAY
endif
ifeq ($(BOARD_USES_ARGB8888), true)
LOCAL_CFLAGS += -DUSES_ARGB8888
LOCAL_C_INCLUDES += \
        $(TOP)/hardware/samsung_slsi/exynos/include
endif

LOCAL_MODULE:= libstagefright_wfd

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
