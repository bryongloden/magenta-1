// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <runtime/mutex.h>
#include <sys/types.h>

#ifndef HID_FIFO_SIZE
#define HID_FIFO_SIZE 4096
#endif
#define HID_FIFO_MASK (HID_FIFO_SIZE-1)

typedef struct {
    uint8_t buf[HID_FIFO_SIZE];
    uint32_t head;
    uint32_t tail;
    bool empty;
    mxr_mutex_t lock;
} mx_hid_fifo_t;

mx_status_t mx_hid_fifo_create(mx_hid_fifo_t** fifo);
void mx_hid_fifo_init(mx_hid_fifo_t* fifo);
size_t mx_hid_fifo_size(mx_hid_fifo_t* fifo);
ssize_t mx_hid_fifo_peek(mx_hid_fifo_t* fifo, uint8_t* out);
ssize_t mx_hid_fifo_read(mx_hid_fifo_t* fifo, uint8_t* buf, size_t len);
ssize_t mx_hid_fifo_write(mx_hid_fifo_t* fifo, const uint8_t* buf, size_t len);

void mx_hid_fifo_dump(mx_hid_fifo_t* fifo);
