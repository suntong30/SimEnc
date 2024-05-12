LOCAL_PATH:= $(call my-dir)

xdelta3_cflags := \
        -O3 \
        -fno-function-sections -fno-data-sections -fno-inline \
        -DSUPPORT_ANDROID_PRELINK_TAGS \
        -DGENERIC_ENCODE_TABLES=0 \
        -DREGRESSION_TEST=0 \
        -DSECONDARY_DJW=1 \
        -DSECONDARY_FGK=1 \
        -DXD3_DEBUG=0 \
        -DXD3_MAIN=0 \
        -DXD3_POSIX=1 \
        -DXD3_USE_LARGEFILE64=1

include $(CLEAR_VARS)

LOCAL_CFLAGS += $(xdelta3_cflags)
LOCAL_SRC_FILES := xdelta3.c
LOCAL_C_INCLUDES:= $(LOCAL_PATH)/
LOCAL_MODULE := libxdelta3
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_CFLAGS += $(xdelta3_cflags)
LOCAL_SRC_FILES := xdelta3.c
LOCAL_C_INCLUDES:= $(LOCAL_PATH)/
LOCAL_MODULE := libxdelta3_host
include $(BUILD_HOST_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_CFLAGS += $(xdelta3_cflags) -DXD3_MAIN=1
LOCAL_SRC_FILES := xdelta3.c
LOCAL_C_INCLUDES:= $(LOCAL_PATH)/
LOCAL_MODULE := xdelta3
include $(BUILD_HOST_EXECUTABLE)
