LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

USE_AM_SOFT_DEMUXER_CODEC := true

LOCAL_SRC_FILES:=                     \
        GraphicBufferSource.cpp       \
        OMX.cpp                       \
        OMXMaster.cpp                 \
        OMXNodeInstance.cpp           \
        SimpleSoftOMXComponent.cpp    \
        SoftOMXComponent.cpp          \
        SoftOMXPlugin.cpp             \
        SoftVideoDecoderOMXComponent.cpp \

LOCAL_C_INCLUDES += \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/hardware \
        $(TOP)/frameworks/native/include/media/openmax

LOCAL_SHARED_LIBRARIES :=               \
        libbinder                       \
        libmedia                        \
        libutils                        \
        liblog                          \
        libui                           \
        libgui                          \
        libcutils                       \
        libstagefright_foundation       \
        libdl

ifeq ($(USE_AM_SOFT_DEMUXER_CODEC),true)
LOCAL_CFLAGS += -DUSE_AM_SOFT_DEMUXER_CODEC
endif

LOCAL_MODULE:= libstagefright_omx

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(call all-makefiles-under,$(LOCAL_PATH))
