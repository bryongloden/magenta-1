// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <magenta/syscalls.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <mxio/io.h>

static int netfd = -1;
static uint8_t netmac[6];

#define MAX_FILTER 8

#define NUM_BUFFER_PAGES 8
#define ETH_BUFFER_SIZE 1536
#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

typedef struct eth_buffer eth_buffer_t;
struct eth_buffer {
    uint64_t magic;
    eth_buffer_t* next;
    uint8_t data[0];
};

static eth_buffer_t* eth_buffers = NULL;

void* eth_get_buffer(size_t sz) {
    eth_buffer_t* buf;
    if (sz > ETH_BUFFER_SIZE) {
        return NULL;
    }
    if (eth_buffers == NULL) {
        printf("out of buffers\n");
        return NULL;
    }
    buf = eth_buffers;
    eth_buffers = buf->next;
    buf->next = NULL;
    return buf->data;
}

void eth_put_buffer(void* data) {
    eth_buffer_t* buf = (void*)(((uintptr_t)data) & (~31));
    if (buf->magic != ETH_BUFFER_MAGIC) {
        printf("fatal: eth buffer %p (from %p) bad magic %llx\n", buf, data, buf->magic);
        for (;;)
            ;
    }
    buf->next = eth_buffers;
    eth_buffers = buf;
}

int eth_send(void* data, size_t len) {
    int r = write(netfd, data, len);
    eth_put_buffer(data);
    return r;
}

int eth_add_mcast_filter(const mac_addr_t* addr) {
    return 0;
}

static volatile uint64_t net_timer = 0;

#define TIMER_MS(n) (((uint64_t)(n)) * 1000000ULL)

void netifc_set_timer(uint32_t ms) {
    net_timer = mx_current_time() + TIMER_MS(ms);
}

int netifc_timer_expired(void) {
    if (net_timer == 0) {
        return 0;
    }
    if (mx_current_time() > net_timer) {
        return 1;
    }
    return 0;
}

int netifc_open(void) {
    DIR* dir;
    struct dirent* de;

    if ((dir = opendir("/dev/class/ethernet")) == NULL) {
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        char tmp[128];
        if (de->d_name[0] == '.') {
            continue;
        }
        snprintf(tmp, sizeof(tmp), "/dev/class/ethernet/%s", de->d_name);
        if ((netfd = open(tmp, O_RDWR)) >= 0) {
            break;
        }
    }
    closedir(dir);
    if (netfd < 0) {
        return -1;
    }

    if (read(netfd, netmac, 6) != 6) {
        close(netfd);
        netfd = -1;
        return -1;
    }
    ip6_init(netmac);
    for (int i = 0; i < 8; i++) {
        char* buffer = malloc(sizeof(eth_buffer_t) + ETH_BUFFER_SIZE + 32);
        buffer = (char*)((((uintptr_t)buffer) + 31) & (~31));
        if (buffer) {
            eth_buffer_t* eb = (eth_buffer_t*)buffer;
            eb->magic = ETH_BUFFER_MAGIC;
            eth_put_buffer(buffer + sizeof(eth_buffer_t));
        }
    }
    return 0;
}

void netifc_close(void) {
    close(netfd);
    netfd = -1;
}

int netifc_active(void) {
    return (netfd >= 0);
}

void netifc_poll(void) {
    uint8_t buffer[2048];
    int r;

    for (;;) {
        while ((r = read(netfd, buffer, sizeof(buffer))) > 0) {
            eth_recv(buffer, r);
        }
        if (net_timer) {
            mx_time_t now = mx_current_time();
            if (now > net_timer) {
                break;
            }
            mxio_wait_fd(netfd, MXIO_EVT_READABLE, NULL, net_timer - now + TIMER_MS(1));
        } else {
            mxio_wait_fd(netfd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        }
    }
}
