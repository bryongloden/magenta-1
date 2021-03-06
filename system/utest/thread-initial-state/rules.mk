# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += $(LOCAL_DIR)/init-state.c

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += $(LOCAL_DIR)/x86-64.S
else ifeq ($(ARCH),arm)
MODULE_SRCS += $(LOCAL_DIR)/arm.S
else ifeq ($(ARCH),arm64)
MODULE_SRCS += $(LOCAL_DIR)/arm64.S
endif

MODULE_NAME := thread-initial-state-test

MODULE_LIBS := ulib/unittest ulib/mxio ulib/magenta ulib/musl

include make/module.mk
