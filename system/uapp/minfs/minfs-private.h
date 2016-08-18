// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "vfs.h"
#include "minfs.h"

#define MINFS_HASH_BITS (8)
#define MINFS_BUCKETS (1 << MINFS_HASH_BITS)

typedef struct minfs minfs_t;

struct minfs {
    bitmap_t block_map;
    bitmap_t inode_map;
    bcache_t* bc;
    uint32_t abmblks;
    uint32_t ibmblks;
    minfs_info_t info;
    list_node_t vnode_hash[MINFS_BUCKETS];
};

struct vnode {
    // ops, flags, refcount
    VNODE_BASE_FIELDS

    minfs_t* fs;

    uint32_t ino;
    uint32_t reserved;

    list_node_t hashnode;

    minfs_inode_t inode;
};

extern vnode_ops_t minfs_ops;

#define INO_HASH(ino) fnv1a_tiny(ino, MINFS_HASH_BITS)

// instantiate a vnode from an inode
// the inode must exist in the file system
mx_status_t minfs_get_vnode(minfs_t* fs, vnode_t** out, uint32_t ino);

// instantiate a vnode with a new inode
mx_status_t minfs_new_vnode(minfs_t* fs, vnode_t** out, uint32_t type);

// delete the inode backing a vnode
mx_status_t minfs_del_vnode(vnode_t* vn);

// allocate a new data block and bcache_get_zero() it
block_t* minfs_new_block(minfs_t* fs, uint32_t hint, uint32_t* out_bno, void** bdata);

// write the inode data of this vnode to disk
void minfs_sync_vnode(vnode_t* vn);

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max);
void minfs_dump_info(minfs_info_t* info);

mx_status_t minfs_create(minfs_t** out, bcache_t* bc, minfs_info_t* info);
mx_status_t minfs_load_bitmaps(minfs_t* fs);
void minfs_destroy(minfs_t* fs);

int minfs_mkfs(bcache_t* bc);

mx_status_t minfs_check(bcache_t* bc);

mx_status_t minfs_mount(vnode_t** root_out, bcache_t* bc);

mx_status_t minfs_get_vnode(minfs_t* fs, vnode_t** out, uint32_t ino);

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent);