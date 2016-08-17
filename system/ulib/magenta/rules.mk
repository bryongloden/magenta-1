# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# This library should not depend on libc.
MODULE_COMPILEFLAGS := -ffreestanding

# Both kernel and userspace code needs magenta headers.
GLOBAL_INCLUDES += $(LOCAL_DIR)/include

ifeq ($(ARCH),arm)
MODULE_SRCS += $(LOCAL_DIR)/syscalls-arm32.S
else ifeq ($(ARCH),arm64)
MODULE_SRCS += $(LOCAL_DIR)/syscalls-arm64.S
else ifeq ($(ARCH),x86)
    ifeq ($(SUBARCH),x86-64)
    MODULE_SRCS += $(LOCAL_DIR)/syscalls-x86-64.S
    else
    MODULE_SRCS += $(LOCAL_DIR)/syscalls-x86.S
    endif
endif

MODULE_EXPORT := magenta
MODULE_SO_NAME := magenta

# TODO(mcgrathr): When the vDSO is fully in proper use, then there
# will be no need for libmagenta.so in the runtime filesystem at all.
#MODULE_SO_INSTALL_NAME := -

# All the code this DSO is pure read-only/reentrant code that
# does not need any writable data (except its caller's stack).
# Make it use a simplified, hardened memory layout.
MODULE_LDFLAGS := -T $(BUILDDIR)/rodso.ld
MODULE_EXTRA_OBJS := $(BUILDDIR)/rodso-stamp

include make/module.mk
