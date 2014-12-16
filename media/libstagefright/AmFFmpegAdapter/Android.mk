LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    utils/AmFFmpegByteIOAdapter.cpp \
    utils/AmFFmpegUtils.cpp \
    utils/AmPTSPopulator.cpp \
    extractor/AmFFmpegExtractor.cpp \
    extractor/AmSimpleMediaExtractorPlugin.cpp \
    codec/AmFFmpegCodec.cpp \
    codec/AmVideoCodec.cpp \
    codec/AmAudioCodec.cpp \
    formatters/AACFormatter.cpp \
    formatters/AVCCFormatter.cpp \
    formatters/MPEG42Formatter.cpp \
    formatters/PassthruFormatter.cpp \
    formatters/PCMBlurayFormatter.cpp \
    formatters/StreamFormatter.cpp \
    formatters/VorbisFormatter.cpp \
    formatters/VC1Formatter.cpp \
    formatters/WMAFormatter.cpp \
    formatters/APEFormatter.cpp

LOCAL_C_INCLUDES:= \
    $(TOP)/frameworks/native/include/media/openmax \
    $(TOP)/frameworks/av/include \
    $(TOP)/frameworks/av/media/libstagefright \
    $(TOP)/external/ffmpeg \
    $(LOCAL_PATH)/include

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libamffmpeg \
    libmedia \
    libstagefright \
    libstagefright_foundation \
    libutils

LOCAL_CFLAGS := -D__STDC_CONSTANT_MACROS # For stdint macros used in FFmpeg.

LOCAL_MODULE_TAGS := optional
LOCAL_ARM_MODE := arm
LOCAL_PRELINK_MODULE := false

LOCAL_MODULE:= libamffmpegadapter

include $(BUILD_SHARED_LIBRARY)
