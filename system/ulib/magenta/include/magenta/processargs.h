// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This is a protocol for passing state to a new process
// via a message in a message pipe.

#define MX_PROCARGS_PROTOCOL 0x4150585d // MXPA
#define MX_PROCARGS_VERSION 0x0001000

typedef struct mx_proc_args mx_proc_args_t;

struct mx_proc_args {
    // Protocol and version identifiers to allow for
    // different process start message protocols and
    // versioning of the same.
    uint32_t protocol;
    uint32_t version;

    // Offset from start of message to handle info
    // array, which contains one uint32_t per handle
    // passed along with the message.
    uint32_t handle_info_off;

    // Offset from start of message to arguments and
    // count of arguments.  Arguments are provided as
    // a set of null-terminated utf-8 strings, one
    // after the other.
    uint32_t args_off;
    uint32_t args_num;

    // Offset from start of message to environment strings and count of
    // them.  Environment entries are provided as a set of null-terminated
    // UTF-8 strings, one after the other.  Canonically each string has
    // the form "NAME=VALUE", but nothing enforces this.
    uint32_t environ_off;
    uint32_t environ_num;
};

// Handle Info entries associate a type and optional
// argument with each handle included in the process
// arguments message.
#define MX_HND_INFO(type, arg) (((type)&0xFFFF) | (((arg)&0xFFFF) << 16))
#define MX_HND_INFO_TYPE(n) ((n)&0xFFFF)
#define MX_HND_INFO_ARG(n) (((n) >> 16) & 0xFFFF)

// handle to our own process
#define MX_HND_TYPE_PROC_SELF 1

// handle to the VMO containing the ELF image of the vDSO, to map into children
#define MX_HND_TYPE_VDSO_VMO 2

// Handle to a VMO containing a bootfs format image.
// The "arg" field used with this type is a simple ordinal.
#define MX_HND_TYPE_BOOTFS_VMO 3

// Handle types the mxio library uses
#define MX_HND_TYPE_MXIO_ROOT 0x10
#define MX_HND_TYPE_MXIO_REMOTE 0x11
#define MX_HND_TYPE_MXIO_PIPE 0x12
#define MX_HND_TYPE_MXIO_EVENT 0x13
#define MX_HND_TYPE_MXIO_LOGGER 0x14

// Handle types used by the mojo application model
#define MX_HND_TYPE_APPLICATION_REQUEST 0x100

// Message pipe for dynamic loader service
#define MX_HND_TYPE_LOADER_SVC 0x200

// VM object handle for the main executable file
#define MX_HND_TYPE_EXEC_VMO 0x201

// Handle types for one-off use and prototyping
#define MX_HND_TYPE_USER0 0xFFF0
#define MX_HND_TYPE_USER1 0xFFF1
#define MX_HND_TYPE_USER2 0xFFF2

#define MX_PROC_INFO_MAGIC 0xd0dabb1e
#define MX_PROC_INFO_VERSION 0

// In-process parsed process args structure
typedef struct mx_proc_info mx_proc_info_t;
struct mx_proc_info {
    // Identifier and version of the mx_proc_info object.
    uint32_t magic;   // MX_PROC_INFO_MAGIC
    uint32_t version; // MX_PROC_INFO_VERSION

    // Next available TLS slot. Increment atomically.
    uint32_t next_tls_slot;

    mx_proc_args_t* proc_args;

    mx_handle_t* handle;
    uint32_t* handle_info;
    int handle_count;

    char** argv;
    char** envp;
    int argc;
    int envc;
};

// Dynamic Loader Service Messages
// Used by dynamic loader to obtain objects to link.
typedef struct mx_loader_svc_msg mx_loader_svc_msg_t;
struct mx_loader_svc_msg {
    uint32_t opcode;
    int32_t arg;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t data[0];
};

#define LOADER_SVC_OP_STATUS 0
// reply message, arg=status

#define LOADER_SVC_OP_DONE 1
// Clean shutdown of service

#define LOADER_SVC_OP_LOAD_OBJECT 2
// arg=0, data[] object name (asciiz)
// reply includes vmo handle on success

#define LOADER_SVC_OP_DEBUG_PRINT 3
// arg=0, data[] debug text (asciiz)

#ifdef __cplusplus
}
#endif
