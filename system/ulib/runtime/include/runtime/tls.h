// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/prctl.h>
#include <magenta/syscalls.h>
#include <magenta/tlsroot.h>
#include <system/compiler.h>

__BEGIN_CDECLS

typedef uint32_t mxr_tls_t;

#define MXR_TLS_SLOT_MAX ((mxr_tls_t)256)
#define MXR_TLS_SLOT_SELF ((mxr_tls_t)0)
#define MXR_TLS_SLOT_ERRNO ((mxr_tls_t)1)
#define MXR_TLS_SLOT_INVALID ((mxr_tls_t)-1)

#pragma GCC visibility push(hidden)

// Allocate a thread local storage slot. mxr_tls_t slots do not have
// associated destructors and cannot be reclaimed.
mxr_tls_t mxr_tls_allocate(void);

// Get and set the value in a thread local storage slot.
static inline void* mxr_tls_get(mxr_tls_t slot);
static inline void mxr_tls_set(mxr_tls_t slot, void* value);

// Get or set the tls root structure for the current thread. These
// only need to be called by the implementations of e.g. mxr_threads
// or pthreads, not directly by users of thread local storage.
static inline mx_tls_root_t* mxr_tls_root_get(void);
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot);

#if defined(__aarch64__)
static inline mx_tls_root_t* mxr_tls_root_get(void) {
    mx_tls_root_t* tlsroot;
    __asm__ volatile("mrs %0, tpidr_el0"
                     : "=r"(tlsroot));
    return tlsroot;
}
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot) {
    __asm__ volatile("msr tpidr_el0, %0"
                     :
                     : "r"(tlsroot));
    return NO_ERROR;
}

#elif defined(__arm__)
static inline mx_tls_root_t* mxr_tls_root_get(void) {
    mx_tls_root_t* tlsroot;
    __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 3"
                         : "=r"(tlsroot));
    return tlsroot;
}
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot) {
    // TODO(kulakowski) Thread self handle.
    mx_handle_t self = 0;
    return mx_thread_arch_prctl(self, ARCH_SET_CP15_READONLY, (uintptr_t*)&tlsroot);
}

#elif defined(__x86_64__)
static inline mx_tls_root_t* mxr_tls_root_get(void) {
    mx_tls_root_t* tlsroot;
    __asm__ __volatile__("mov %%fs:0,%0"
                         : "=r"(tlsroot));
    return tlsroot;
}
static inline mx_status_t mxr_tls_root_set(mx_tls_root_t* tlsroot) {
    // TODO(kulakowski) Thread self handle.
    mx_handle_t self = 0;
    return mx_thread_arch_prctl(self, ARCH_SET_FS, (uintptr_t*)&tlsroot);
}

#else
#error Unsupported architecture

#endif

mxr_tls_t mxr_tls_allocate(void);

static inline void* mxr_tls_get(mxr_tls_t slot) {
    return mxr_tls_root_get()->slots[slot];
}

static inline void mxr_tls_set(mxr_tls_t slot, void* value) {
    mxr_tls_root_get()->slots[slot] = value;
}

#pragma GCC visibility pop

__END_CDECLS
