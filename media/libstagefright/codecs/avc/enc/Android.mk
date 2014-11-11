LOCAL_PATH := $(call my-dir)
#########################################
include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := libstagefright_avcenc.a  

include $(BUILD_MULTI_PREBUILT)  

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    platform/AML_HWEncoder.cpp \
    platform/amvenclib.cpp \
    platform/fill_buffer.cpp \
    platform/rate_control.cpp \
    platform/mv.cpp \
    platform/pred_neon_asm.s \
    platform/pred.cpp

LOCAL_MODULE := libstagefright_platformenc
LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/src \
    $(LOCAL_PATH)/../common/include \
    $(TOP)/frameworks/av/media/libstagefright/include \
    $(TOP)/frameworks/native/include/media/openmax

LOCAL_SHARED_LIBRARIES := \
        libutils liblog

LOCAL_CFLAGS := \
    -DOSCL_IMPORT_REF= -DOSCL_UNUSED_ARG= -DOSCL_EXPORT_REF=

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SoftAVCEncoder.cpp

ifeq ($(ARCH_ARM_HAVE_NEON),true)
LOCAL_SRC_FILES += ColorConverter_neon.s
endif

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/hardware \
        frameworks/native/include/media/openmax \
        $(LOCAL_PATH)/src \
        $(LOCAL_PATH)/include \
        $(LOCAL_PATH)/../common/include \
        $(LOCAL_PATH)/../common

LOCAL_CFLAGS := \
    -D__arm__ \
    -DMULTI_THREAD -DDISABLE_DEBLOCK  \
    -DOSCL_IMPORT_REF= -DOSCL_UNUSED_ARG= -DOSCL_EXPORT_REF=

ifeq ($(ARCH_ARM_HAVE_NEON),true)
LOCAL_CFLAGS += -DASM_OPT
endif

LOCAL_STATIC_LIBRARIES := \
        libstagefright_avcenc

LOCAL_SHARED_LIBRARIES := \
        libstagefright \
        libstagefright_avc_common \
        libstagefright_enc_common \
        libstagefright_foundation \
        libstagefright_omx \
        libutils \
        libui \
        liblog \
        libdl


LOCAL_MODULE := libstagefright_soft_h264enc
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
