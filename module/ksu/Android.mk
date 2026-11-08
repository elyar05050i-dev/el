# SPDX-License-Identifier: GPL-2.0-only
LOCAL_PATH := $(call my-dir)

# Prebuilt static libraries used by file manager
include $(CLEAR_VARS)
LOCAL_MODULE := libarchive_static_prebuilt
LOCAL_SRC_FILES := vendor/file_manager/prebuilt/arm64-v8a/libarchive.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := liblzma_static_prebuilt
LOCAL_SRC_FILES := vendor/file_manager/prebuilt/arm64-v8a/liblzma.a
include $(PREBUILT_STATIC_LIBRARY)

# KSU standalone RPC daemon
include $(CLEAR_VARS)
LOCAL_MODULE := susfs_ksu_daemon
LOCAL_CPP_FEATURES += exceptions
LOCAL_CPPFLAGS += -std=c++20 -fexceptions -fno-rtti -DSUSFS_KSU_STANDALONE
LOCAL_SRC_FILES := \
    ksu/ksu_daemon.cpp \
    ksu/susfs_ksu_rpc.cpp \
    management/embedded.cpp management/service.cpp \
    management/backend.cpp management/config.cpp management/config_transfer.cpp \
    management/inventory.cpp management/common.cpp \
    vendor/file_manager/cJSON.cpp \
    vendor/file_manager/app_metadata_service.cpp vendor/file_manager/file_manager_service.cpp \
    vendor/file_manager/file_manager_api.cpp vendor/file_manager/archive_service.cpp \
    vendor/file_manager/archive_browser_service.cpp vendor/file_manager/image_preview_service.cpp \
    vendor/file_manager/file_broker.cpp \
    vendor/file_manager/storage.cpp
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/ksu \
    $(LOCAL_PATH)/vendor/file_manager \
    $(LOCAL_PATH)/vendor/file_manager/third_party/libarchive/include \
    $(LOCAL_PATH)/management \
    $(LOCAL_PATH)/kernel
LOCAL_STATIC_LIBRARIES := libarchive_static_prebuilt liblzma_static_prebuilt
LOCAL_LDLIBS += -lz -ldl -llog -lm
LOCAL_ARM_MODE := arm64
include $(BUILD_EXECUTABLE)