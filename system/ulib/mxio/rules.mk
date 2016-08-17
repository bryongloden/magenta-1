# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/bootfs.c \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/dispatcher-v2.c \
    $(LOCAL_DIR)/logger.c \
    $(LOCAL_DIR)/null.c \
    $(LOCAL_DIR)/pipe.c \
    $(LOCAL_DIR)/remoteio.c \
    $(LOCAL_DIR)/socket.c \
    $(LOCAL_DIR)/unistd.c \
    $(LOCAL_DIR)/startup-handles.c \
    $(LOCAL_DIR)/stubs.c \
    $(LOCAL_DIR)/loader-service.c \

MODULE_EXPORT := mxio

MODULE_SO_NAME := mxio
MODULE_STATIC_LIBS := ulib/runtime
MODULE_LIBS := ulib/magenta ulib/musl

include make/module.mk
