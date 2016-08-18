// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <mxio/vfs.h>

#include "minfs-private.h"

// Allocate a new data block from the block bitmap.
// Return the underlying block (obtained via bcache_get()).
// If hint is nonzero it indicates which block number
// to start the search for free blocks from.
block_t* minfs_new_block(minfs_t* fs, uint32_t hint, uint32_t* out_bno, void** bdata) {
    uint32_t bno = bitmap_alloc(&fs->block_map, hint);
    if ((bno == BITMAP_FAIL) && (hint != 0)) {
        bno = bitmap_alloc(&fs->block_map, 0);
    }
    if (bno == BITMAP_FAIL) {
        return NULL;
    }

    // obtain the block of the alloc bitmap we need
    block_t* block_abm;
    void* bdata_abm;
    if ((block_abm = bcache_get(fs->bc, fs->info.abm_block + (bno / MINFS_BLOCK_BITS), &bdata_abm)) == NULL) {
        bitmap_clr(&fs->block_map, bno);
        return NULL;
    }

    // obtain the block we're allocating
    block_t* block;
    if ((block = bcache_get_zero(fs->bc, bno, bdata)) == NULL) {
        bitmap_clr(&fs->block_map, bno);
        bcache_put(fs->bc, block_abm, 0);
        return NULL;
    }

    // commit the bitmap
    memcpy(bdata_abm, fs->block_map.map + ((bno / MINFS_BLOCK_BITS) * (MINFS_BLOCK_BITS / 64)), MINFS_BLOCK_SIZE);
    bcache_put(fs->bc, block_abm, BLOCK_DIRTY);
    *out_bno = bno;
    return block;
}

// Obtain the nth block of a vnode.
// If alloc is true, allocate that block if it doesn't already exist.
static block_t* vn_get_block(vnode_t* vn, uint32_t n, void** bdata, bool alloc) {
#if 0
    uint32_t hint = ((vn->fs->info.block_count - vn->fs->info.dat_block) / 256) * (vn->ino % 256);
#else
    uint32_t hint = 0;
#endif
    // direct blocks are simple... is there an entry in dnum[]?
    if (n < MINFS_DIRECT) {
        uint32_t bno;
        if ((bno = vn->inode.dnum[n]) == 0) {
            if (alloc) {
                block_t* blk = minfs_new_block(vn->fs, hint, &bno, bdata);
                if (blk != NULL) {
                    vn->inode.dnum[n] = bno;
                    vn->inode.block_count++;
                    minfs_sync_vnode(vn);
                }
                return blk;
            } else {
                return NULL;
            }
        }
        return bcache_get(vn->fs->bc, bno, bdata);
    }

    // for indirect blocks, adjust past the direct blocks
    n -= MINFS_DIRECT;

    // determine indices into the indirect block list and into
    // the block list in the indirect block
    uint32_t i = n / (MINFS_BLOCK_SIZE / sizeof(uint32_t));
    uint32_t j = n % (MINFS_BLOCK_SIZE / sizeof(uint32_t));

    if (i >= MINFS_INDIRECT) {
        return NULL;
    }

    uint32_t ibno;
    block_t* iblk;
    uint32_t* ientry;
    uint32_t iflags = 0;

    // look up the indirect bno
    if ((ibno = vn->inode.inum[i]) == 0) {
        if (!alloc) {
            return NULL;
        }
        // allocate a new indirect block
        if ((iblk = minfs_new_block(vn->fs, 0, &ibno, (void**) &ientry)) == NULL) {
            return NULL;
        }
        // record new indirect block in inode, note that we need to update
        vn->inode.block_count++;
        vn->inode.inum[i] = ibno;
        iflags = BLOCK_DIRTY;
    } else {
        if ((iblk = bcache_get(vn->fs->bc, ibno, (void**) &ientry)) == NULL) {
            error("minfs: cannot read indirect block @%u\n", ibno);
            return NULL;
        }
    }

    uint32_t bno;
    block_t* blk = NULL;
    if ((bno = ientry[j]) == 0) {
        if (alloc) {
            // allocate a new block
            blk = minfs_new_block(vn->fs, hint, &bno, bdata);
            if (blk != NULL) {
                vn->inode.block_count++;
                ientry[j] = bno;
                iflags = BLOCK_DIRTY;
            }
        }
    } else {
        blk = bcache_get(vn->fs->bc, bno, bdata);
    }

    // release indirect block, updating if necessary
    // and update the inode as well if we changed it
    bcache_put(vn->fs->bc, iblk, iflags);
    if (iflags & BLOCK_DIRTY) {
        minfs_sync_vnode(vn);
    }

    return blk;
}

static inline void vn_put_block(vnode_t* vn, block_t* blk) {
    bcache_put(vn->fs->bc, blk, 0);
}

static inline void vn_put_block_dirty(vnode_t* vn, block_t* blk) {
    bcache_put(vn->fs->bc, blk, BLOCK_DIRTY);
}

#define DIR_CB_DONE 0
#define DIR_CB_NEXT 1
#define DIR_CB_SAVE 2
#define DIR_CB_SAVE_SYNC 3

typedef struct dir_args {
    const char* name;
    size_t len;
    uint32_t ino;
    uint32_t type;
    uint32_t reclen;
} dir_args_t;

static mx_status_t cb_dir_find(vnode_t* vndir, minfs_dirent_t* de, dir_args_t* args) {
    if ((de->ino != 0) && (de->namelen == args->len) &&
        (!memcmp(de->name, args->name, args->len))) {
        args->ino = de->ino;
        args->type = de->type;
        return DIR_CB_DONE;
    } else {
        return DIR_CB_NEXT;
    }
}

// caller is expected to prevent unlink of "." or ".."
static mx_status_t cb_dir_unlink(vnode_t* vndir, minfs_dirent_t* de, dir_args_t* args) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return DIR_CB_NEXT;
    }
    vnode_t* vn;
    mx_status_t status;
    if ((status = minfs_get_vnode(vndir->fs, &vn, de->ino)) < 0) {
        return status;
    }
    // inode w/ link_count zero will be destroyed upon vn_release()
    if (vn->inode.magic == MINFS_MAGIC_DIR) {
        if (vn->inode.dirent_count != 2) {
            // if we have more than "." and "..", not empty, cannot unlink
            return ERR_BAD_STATE;
        }
        if (vn->inode.link_count != 2) {
            error("minfs: directory ino#%u linkcount %u\n",
                  vn->ino, vn->inode.link_count);
            return ERR_BAD_STATE;
        }
        //TODO: release all blocks
        vn->inode.link_count = 0;
    } else {
        vn->inode.link_count--;
    }
    // erase dirent (convert to empty entry), decrement dirent count
    de->ino = 0;
    vndir->inode.dirent_count--;
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_append(vnode_t* vndir, minfs_dirent_t* de, dir_args_t* args) {
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > de->reclen) {
            return DIR_CB_NEXT;
        }
    } else {
        // filled entry, can we sub-divide?
        uint32_t size = SIZEOF_MINFS_DIRENT(de->namelen);
        if (size > de->reclen) {
            error("bad reclen %u < %u\n", de->reclen, size);
            return DIR_CB_DONE;
        }
        uint32_t extra = de->reclen - size;
        if (extra < args->reclen) {
            return DIR_CB_NEXT;
        }
        // shrink existing entry
        de->reclen = size;
        // create new entry in the remaining space
        de = ((void*)de) + size;
        de->reclen = extra;
    }
    de->ino = args->ino;
    de->type = args->type;
    de->namelen = args->len;
    memcpy(de->name, args->name, args->len);
    vndir->inode.dirent_count++;
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t vn_dir_for_each(vnode_t* vn, dir_args_t* args,
                                   mx_status_t (*func)(vnode_t*, minfs_dirent_t*, dir_args_t*)) {
    for (unsigned n = 0; n < vn->inode.block_count; n++) {
        block_t* blk;
        void* data;
        if ((blk = vn_get_block(vn, n, &data, false)) == NULL) {
            error("vn_dir: vn=%p missing block %u\n", vn, n);
            return ERR_NOT_FOUND;
        }
        uint32_t size = MINFS_BLOCK_SIZE;
        minfs_dirent_t* de = data;
        while (size > MINFS_DIRENT_SIZE) {
            //fprintf(stderr,"DE ino=%u rlen=%u nlen=%u\n", de->ino, de->reclen, de->namelen);
            uint32_t rlen = de->reclen;
            if ((rlen > size) || (rlen & 3)) {
                error("vn_dir: vn=%p bad reclen %u > %u\n", vn, rlen, size);
                break;
            }
            if (de->ino != 0) {
                if ((de->namelen == 0) || (de->namelen > (rlen - MINFS_DIRENT_SIZE))) {
                    error("vn_dir: vn=%p bad namelen %u / %u\n", vn, de->namelen, rlen);
                    break;
                }
            }
            mx_status_t status;
            switch ((status = func(vn, de, args))) {
            case DIR_CB_NEXT:
                break;
            case DIR_CB_SAVE:
                vn_put_block_dirty(vn, blk);
                return NO_ERROR;
            case DIR_CB_SAVE_SYNC:
                vn_put_block_dirty(vn, blk);
                minfs_sync_vnode(vn);
                return NO_ERROR;
            case DIR_CB_DONE:
            default:
                vn_put_block(vn, blk);
                return status;
            }
            de = ((void*) de) + rlen;
            size -= rlen;
        }
        vn_put_block(vn, blk);
    }
    return ERR_NOT_FOUND;
}

static void fs_release(vnode_t* vn) {
    trace(MINFS, "minfs_release() vn=%p(#%u)\n", vn, vn->ino);
}

static mx_status_t fs_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    trace(MINFS, "minfs_open() vn=%p(#%u)\n", vn, vn->ino);
    return NO_ERROR;
}

static mx_status_t fs_close(vnode_t* vn) {
    trace(MINFS, "minfs_close() vn=%p(#%u)\n", vn, vn->ino);
    return NO_ERROR;
}

// not possible to have a block at or past this one
// due to the limitations of the inode and indirect blocks
#define MAX_FILE_BLOCK (MINFS_DIRECT + MINFS_INDIRECT * (MINFS_BLOCK_SIZE / sizeof(uint32_t)))

static ssize_t fs_read(vnode_t* vn, void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_read() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);

    // clip to EOF
    if (off >= vn->inode.size) {
        return 0;
    }
    if (len > (vn->inode.size - off)) {
        len = vn->inode.size - off;
    }

    void* start = data;
    uint32_t n = off / MINFS_BLOCK_SIZE;
    size_t adjust = off % MINFS_BLOCK_SIZE;

    while ((len > 0) && (n < MAX_FILE_BLOCK)) {
        size_t xfer;
        if (len > (MINFS_BLOCK_SIZE - adjust)) {
            xfer = MINFS_BLOCK_SIZE - adjust;
        } else {
            xfer = len;
        }

        block_t* blk;
        void* bdata;
        if ((blk = vn_get_block(vn, n, &bdata, true)) == NULL) {
            break;
        }
        memcpy(data, bdata + adjust, xfer);
        vn_put_block(vn, blk);

        adjust = 0;
        len -= xfer;
        data += xfer;
        n++;
    }
    return data - start;
}

static ssize_t fs_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_write() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);

    const void* start = data;
    uint32_t n = off / MINFS_BLOCK_SIZE;
    size_t adjust = off % MINFS_BLOCK_SIZE;

    while ((len > 0) && (n < MAX_FILE_BLOCK)) {
        size_t xfer;
        if (len > (MINFS_BLOCK_SIZE - adjust)) {
            xfer = MINFS_BLOCK_SIZE - adjust;
        } else {
            xfer = len;
        }

        block_t* blk;
        void* bdata;
        if ((blk = vn_get_block(vn, n, &bdata, true)) == NULL) {
            break;
        }
        memcpy(bdata + adjust, data, xfer);
        vn_put_block_dirty(vn, blk);

        adjust = 0;
        len -= xfer;
        data += xfer;
        n++;
    }

    len = data - start;
    if ((off + len) > vn->inode.size) {
        vn->inode.size = off + len;
        minfs_sync_vnode(vn);
    }
    return data - start;
}

static mx_status_t fs_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    trace(MINFS, "minfs_lookup() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        error("not directory\n");
        return ERR_NOT_SUPPORTED;
    }
    dir_args_t args = {
        .name = name,
        .len = len,
    };
    mx_status_t status;
    if ((status = vn_dir_for_each(vn, &args, cb_dir_find)) < 0) {
        return status;
    }
    if ((status = minfs_get_vnode(vn->fs, &vn, args.ino)) < 0) {
        return status;
    }
    *out = vn;
    return NO_ERROR;
}

static mx_status_t fs_getattr(vnode_t* vn, vnattr_t* a) {
    trace(MINFS, "minfs_getattr() vn=%p(#%u)\n", vn, vn->ino);
    a->inode = vn->ino;
    a->size = vn->inode.size;
    a->mode = DTYPE_TO_VTYPE(MINFS_MAGIC_TYPE(vn->inode.magic));
    return NO_ERROR;
}

typedef struct dircookie {
    uint32_t used;   // not the first call
    uint32_t index;  // block index
    uint32_t size;   // size remaining
    uint32_t seqno;  // inode seq no
} dircookie_t;

static mx_status_t fs_readdir(vnode_t* vn, void* cookie, void* dirents, size_t len) {
    trace(MINFS, "minfs_readdir() vn=%p(#%u) cookie=%p len=%zd\n", vn, vn->ino, cookie, len);
    dircookie_t* dc = cookie;
    vdirent_t* out = dirents;

    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }

    uint32_t idx;
    uint32_t sz;
    if (dc->used) {
        if (dc->seqno != vn->inode.seq_num) {
            // directory has been modified
            // stop returning entries
            dc->index = -1;
            return 0;
        }
        idx = dc->index;
        sz = dc->size;
    } else {
        idx = 0;
        sz = MINFS_BLOCK_SIZE;
    }

    for (;;) {
        minfs_dirent_t* de;
        block_t* blk;
        if ((blk = vn_get_block(vn, idx, (void**) &de, false)) == NULL) {
            goto done;
        }

        // advance to old position if continuing from before
        de = ((void*)de) + (MINFS_BLOCK_SIZE - sz);

        while (sz >= sizeof(minfs_dirent_t)) {
            if ((de->reclen > sz) || (de->reclen & 3) || (de->reclen < MINFS_DIRENT_SIZE) ||
                (de->namelen > (de->reclen - MINFS_DIRENT_SIZE))) {
                // malformed entry
                vn_put_block(vn, blk);
                goto fail;
            }
            if (de->ino) {
                mx_status_t status;
                if ((status = vfs_fill_dirent(out, len, de->name, de->namelen, de->type)) < 0) {
                    // no more space
                    vn_put_block(vn, blk);
                    goto done;
                }
                out = ((void*) out) + status;
            }
            sz -= de->reclen;
            de = ((void*) de) + de->reclen;
        }

        vn_put_block(vn, blk);
        idx++;
        sz = MINFS_BLOCK_SIZE;
    }

done:
    // save our place in the dircookie
    dc->used = 1;
    dc->index = idx;
    dc->size = sz;
    dc->seqno = vn->inode.seq_num;
    return ((void*) out) - dirents;

fail:
    // mark dircookie so further reads return 0
    dc->index = -1;
    dc->used = 1;
    return ERR_IO;
}

static mx_status_t fs_create(vnode_t* vndir, vnode_t** out,
                             const char* name, size_t len, uint32_t mode) {
    trace(MINFS, "minfs_create() vn=%p(#%u) name='%.*s' mode=%#x\n",
          vndir, vndir->ino, (int)len, name, mode);
    if (vndir->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }
    dir_args_t args = {
        .name = name,
        .len = len,
    };
    // ensure file does not exist
    mx_status_t status;
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_find)) != ERR_NOT_FOUND) {
        return ERR_IO; //TODO: err exists
    }

    // creating a directory?
    uint32_t type = S_ISDIR(mode) ? MINFS_TYPE_DIR : MINFS_TYPE_FILE;

    // mint a new inode and vnode for it
    vnode_t* vn;
    if ((status = minfs_new_vnode(vndir->fs, &vn, type)) < 0) {
        return status;
    }

    // add directory entry for the new child node
    args.ino = vn->ino;
    args.type = type;
    args.reclen = SIZEOF_MINFS_DIRENT(len);
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_append)) < 0) {
        //TODO: handle "block full" by creating a new directory block
        error("minfs_create() dir append failed %d\n", status);
        return status;
    }
    // bump the directory inode's seqno and dirent count
    vndir->inode.seq_num++;
    vndir->inode.dirent_count++;
    minfs_sync_vnode(vndir);

    if (type == MINFS_TYPE_DIR) {
        void* bdata;
        block_t* blk;
        if ((blk = minfs_new_block(vndir->fs, 0, vn->inode.dnum + 0, &bdata)) == NULL) {
            panic("failed to create directory");
        }
        minfs_dir_init(bdata, vn->ino, vndir->ino);
        bcache_put(vndir->fs->bc, blk, BLOCK_DIRTY);
        vn->inode.block_count = 1;
        vn->inode.dirent_count = 2;
        vn->inode.size = MINFS_BLOCK_SIZE;
        minfs_sync_vnode(vn);
    }
    *out = vn;
    return NO_ERROR;
}

static ssize_t fs_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                        size_t in_len, void* out_buf, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_unlink(vnode_t* vn, const char* name, size_t len) {
    trace(MINFS, "minfs_unlink() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }
    dir_args_t args = {
        .name = name,
        .len = len,
    };
    return vn_dir_for_each(vn, &args, cb_dir_unlink);
}

vnode_ops_t minfs_ops = {
    .release = fs_release,
    .open = fs_open,
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .lookup = fs_lookup,
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .create = fs_create,
    .ioctl = fs_ioctl,
    .unlink = fs_unlink,
};

