#!/usr/bin/env python3
"""Host tests for opt-in read-only persist:/time/ats_2 ext4 RTC offset parser and policy."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'xnu/fuxi-h1/fuxi_rtc_persist.h'
KEYS_C = ROOT / 'xnu/fuxi-h1/fuxi_keys.c'
UFS_C = ROOT / 'xnu/fuxi-unixnu/sweet_ufs.c'

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuxi_rtc_persist.h"

struct mem_disk {
    uint8_t *data;
    uint64_t blocks_4k;
    unsigned reads;
    unsigned fail_on_read; /* 1-based; 0 = never fail */
    unsigned oob_reads;
};

static int
mem_read_4k(void *context, uint64_t lba_4k, void *dst_4k)
{
    struct mem_disk *disk = (struct mem_disk *)context;
    disk->reads++;
    if (lba_4k >= disk->blocks_4k) {
        disk->oob_reads++;
        return -1;
    }
    if (disk->fail_on_read != 0u && disk->reads == disk->fail_on_read) {
        return -1;
    }
    memcpy(dst_4k, disk->data + lba_4k * 4096ull, 4096u);
    return 0;
}

static void
put_le16(uint8_t *ptr, uint16_t val)
{
    ptr[0] = (uint8_t)(val & 0xffu);
    ptr[1] = (uint8_t)((val >> 8) & 0xffu);
}

static void
put_le32(uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val & 0xffu);
    ptr[1] = (uint8_t)((val >> 8) & 0xffu);
    ptr[2] = (uint8_t)((val >> 16) & 0xffu);
    ptr[3] = (uint8_t)((val >> 24) & 0xffu);
}

static void
put_le64(uint8_t *ptr, uint64_t val)
{
    put_le32(ptr, (uint32_t)(val & 0xffffffffull));
    put_le32(ptr + 4, (uint32_t)(val >> 32));
}

struct ext4_builder {
    uint8_t *img;
    uint32_t bsize;
    uint32_t fs_blocks;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t itable_blk;
    uint32_t incompat;
};

static void
builder_init(struct ext4_builder *bld, uint8_t *img, uint32_t total_4k_blocks,
    uint32_t bsize, uint32_t inode_size, uint32_t desc_size, uint32_t incompat)
{
    memset(img, 0, (size_t)total_4k_blocks * 4096u);
    bld->img = img;
    bld->bsize = bsize;
    bld->fs_blocks = total_4k_blocks * (4096u / bsize);
    bld->inode_size = inode_size;
    bld->desc_size = desc_size;
    bld->itable_blk = (bsize == 1024u) ? 4u : 4u;
    bld->incompat = incompat;

    uint32_t log_bsize = (bsize == 1024u) ? 0u : (bsize == 2048u ? 1u : 2u);
    uint8_t *sb = img + 1024u;
    put_le32(sb + 0x00u, 128u);                              /* s_inodes_count */
    put_le32(sb + 0x04u, bld->fs_blocks);                    /* s_blocks_count_lo */
    put_le32(sb + 0x14u, (bsize == 1024u) ? 1u : 0u);        /* s_first_data_block */
    put_le32(sb + 0x18u, log_bsize);                         /* s_log_block_size */
    put_le32(sb + 0x20u, bld->fs_blocks);                    /* s_blocks_per_group */
    put_le32(sb + 0x28u, 128u);                              /* s_inodes_per_group */
    put_le16(sb + 0x38u, 0xef53u);                           /* s_magic */
    put_le32(sb + 0x4cu, 1u);                                /* s_rev_level */
    put_le16(sb + 0x58u, (uint16_t)inode_size);              /* s_inode_size */
    put_le32(sb + 0x60u, incompat);                          /* s_feature_incompat */
    if ((incompat & FUXI_EXT4_INCOMPAT_64BIT) != 0u) {
        put_le16(sb + 0xfeu, (uint16_t)desc_size);
        put_le32(sb + 0x150u, 0u);
    } else if (desc_size == 32u) {
        put_le16(sb + 0xfeu, 0u);
    }

    uint32_t gdt_blk = (bsize == 1024u) ? 2u : 1u;
    uint8_t *bgd = img + (size_t)gdt_blk * bsize;
    put_le32(bgd + 8u, bld->itable_blk);
    if ((incompat & FUXI_EXT4_INCOMPAT_64BIT) != 0u) {
        put_le32(bgd + 40u, 0u);
    }
}

static uint8_t *
builder_inode(struct ext4_builder *bld, uint32_t ino)
{
    size_t off = (size_t)bld->itable_blk * bld->bsize + (size_t)(ino - 1u) * bld->inode_size;
    return bld->img + off;
}

static void
builder_set_inode_extent0(struct ext4_builder *bld, uint32_t ino, uint16_t mode,
    uint64_t size, uint32_t phys_blk, uint16_t ee_len)
{
    uint8_t *raw = builder_inode(bld, ino);
    put_le16(raw + 0x00u, mode);
    put_le32(raw + 0x04u, (uint32_t)(size & 0xffffffffull));
    put_le32(raw + 0x20u, FUXI_EXT4_FL_EXTENTS);
    put_le32(raw + 0x6cu, (uint32_t)(size >> 32));
    uint8_t *ib = raw + 0x28u;
    put_le16(ib + 0u, 0xf30au);
    put_le16(ib + 2u, 1u);
    put_le16(ib + 4u, 4u);
    put_le16(ib + 6u, 0u);
    put_le32(ib + 12u, 0u);
    put_le16(ib + 16u, ee_len);
    put_le16(ib + 18u, 0u);
    put_le32(ib + 20u, phys_blk);
}

static void
builder_put_dirent(uint8_t *blk, uint32_t off, uint32_t ino, uint16_t rec_len,
    uint8_t ftype, const char *name)
{
    uint8_t nlen = (uint8_t)strlen(name);
    put_le32(blk + off + 0u, ino);
    put_le16(blk + off + 4u, rec_len);
    blk[off + 6u] = nlen;
    blk[off + 7u] = ftype;
    memcpy(blk + off + 8u, name, nlen);
}

static void
build_standard_persist(struct ext4_builder *bld, uint8_t *img, uint32_t total_4k,
    uint32_t bsize, uint32_t inode_size, uint32_t desc_size, uint32_t incompat,
    uint64_t ats2_ms)
{
    builder_init(bld, img, total_4k, bsize, inode_size, desc_size, incompat);
    uint32_t root_dir_blk = 20u;
    uint32_t time_dir_blk = 21u;
    uint32_t ats2_data_blk = 22u;

    builder_set_inode_extent0(bld, 2u, FUXI_EXT4_S_IFDIR | 0771u, bsize, root_dir_blk, 1u);
    uint8_t *rdir = img + (size_t)root_dir_blk * bsize;
    builder_put_dirent(rdir, 0u, 2u, 12u, 2u, ".");
    builder_put_dirent(rdir, 12u, 2u, 12u, 2u, "..");
    builder_put_dirent(rdir, 24u, 15u, (uint16_t)(bsize - 24u), 2u, "time");

    builder_set_inode_extent0(bld, 15u, FUXI_EXT4_S_IFDIR | 0770u, bsize, time_dir_blk, 1u);
    uint8_t *tdir = img + (size_t)time_dir_blk * bsize;
    builder_put_dirent(tdir, 0u, 15u, 12u, 2u, ".");
    builder_put_dirent(tdir, 12u, 2u, 12u, 2u, "..");
    builder_put_dirent(tdir, 24u, 20u, 16u, 1u, "ats_1");
    builder_put_dirent(tdir, 40u, 21u, (uint16_t)(bsize - 40u), 1u, "ats_2");

    builder_set_inode_extent0(bld, 20u, FUXI_EXT4_S_IFREG | 0600u, 8u, 23u, 1u);
    put_le64(img + (size_t)23u * bsize, ats2_ms - 5000ull);

    builder_set_inode_extent0(bld, 21u, FUXI_EXT4_S_IFREG | 0600u, 8u, ats2_data_blk, 1u);
    put_le64(img + (size_t)ats2_data_blk * bsize, ats2_ms);
}

static void
test_synthetic_valid_layouts(void)
{
    const uint32_t total_4k = 32u;
    uint8_t *img = (uint8_t *)malloc((size_t)total_4k * 4096u);
    assert(img != NULL);
    struct ext4_builder bld;
    struct fuxi_ext4_ro fs;
    struct fuxi_rtc_persist_result res;
    struct mem_disk disk = { img, total_4k, 0u, 0u, 0u };

    /* 1. Standard 4 KiB ext4 with extent leaf, clean */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u,
        FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_EXTENTS, 1795003732213ull);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(res.offset_ms == 1795003732213ull);
    assert(strcmp(res.file_name, "ats_2") == 0);
    assert(!res.needs_recovery);
    assert(disk.oob_reads == 0u);

    /* 2. 4 KiB ext4 with INCOMPAT_RECOVER (needs_recovery=true, like live Android persist) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u,
        FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_RECOVER | FUXI_EXT4_INCOMPAT_EXTENTS,
        1794665920438ull);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(res.offset_ms == 1794665920438ull);
    assert(res.needs_recovery);

    /* 3. 1 KiB block size ext4 (first_data_block=1, GDT at block 2) */
    build_standard_persist(&bld, img, total_4k, 1024u, 128u, 32u,
        FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_EXTENTS, 1795000000123ull);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(res.offset_ms == 1795000000123ull);

    /* 4. 2 KiB block size ext4 with 64BIT feature, 64-byte GDT, 256-byte inodes */
    build_standard_persist(&bld, img, total_4k, 2048u, 256u, 64u,
        FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_EXTENTS | FUXI_EXT4_INCOMPAT_64BIT,
        1795000000456ull);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(res.offset_ms == 1795000000456ull);

    /* 5. Depth-1 extent tree (inode index -> external leaf block) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u,
        FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_EXTENTS, 1795111222333ull);
    {
        uint8_t *raw = builder_inode(&bld, 21u);
        uint8_t *ib = raw + 0x28u;
        put_le16(ib + 0u, 0xf30au);
        put_le16(ib + 2u, 1u);
        put_le16(ib + 4u, 4u);
        put_le16(ib + 6u, 1u); /* depth 1 */
        put_le32(ib + 12u, 0u);
        put_le32(ib + 16u, 25u); /* leaf block 25 */
        put_le16(ib + 20u, 0u);

        uint8_t *leaf = img + 25u * 4096u;
        put_le16(leaf + 0u, 0xf30au);
        put_le16(leaf + 2u, 1u);
        put_le16(leaf + 4u, 340u);
        put_le16(leaf + 6u, 0u); /* depth 0 */
        put_le32(leaf + 12u, 0u);
        put_le16(leaf + 16u, 1u);
        put_le16(leaf + 18u, 0u);
        put_le32(leaf + 20u, 22u);
    }
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(res.offset_ms == 1795111222333ull);

    /* 6. Classic direct + single-indirect block inodes (no EXTENTS flag) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u,
        FUXI_EXT4_INCOMPAT_FILETYPE, 1795222333444ull);
    {
        /* Convert root (2), time (15) to direct/indirect, ats_2 (21) to direct */
        uint8_t *r_ino = builder_inode(&bld, 2u);
        put_le32(r_ino + 0x20u, 0u);
        memset(r_ino + 0x28u, 0, 60u);
        put_le32(r_ino + 0x28u, 20u);

        /* Put ats_2 in logical block 12 (first single-indirect block) of /time */
        uint8_t *t_ino = builder_inode(&bld, 15u);
        put_le32(t_ino + 0x20u, 0u);
        put_le32(t_ino + 0x04u, 13u * 4096u);
        memset(t_ino + 0x28u, 0, 60u);
        put_le32(t_ino + 0x28u + 12u * 4u, 26u); /* single indirect block 26 */
        put_le32(img + 26u * 4096u, 21u);        /* logical blk 12 -> phys blk 21 */

        uint8_t *a_ino = builder_inode(&bld, 21u);
        put_le32(a_ino + 0x20u, 0u);
        memset(a_ino + 0x28u, 0, 60u);
        put_le32(a_ino + 0x28u, 22u);
    }
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(res.offset_ms == 1795222333444ull);

    /* 7. Fallback to /time/ats_1 when ats_2 is unlinked (de_ino=0) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u,
        FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_EXTENTS, 1795003732213ull);
    put_le32(img + 21u * 4096u + 40u, 0u); /* unlink ats_2 */
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    assert(strcmp(res.file_name, "ats_1") == 0);
    assert(res.offset_ms == 1795003732213ull - 5000ull);

    assert(disk.oob_reads == 0u);
    free(img);
}

static void
test_adversarial_and_faults(void)
{
    const uint32_t total_4k = 32u;
    uint8_t *img = (uint8_t *)malloc((size_t)total_4k * 4096u);
    assert(img != NULL);
    struct ext4_builder bld;
    struct fuxi_ext4_ro fs;
    struct fuxi_rtc_persist_result res;
    struct mem_disk disk = { img, total_4k, 0u, 0u, 0u };
    const uint32_t std_incompat = FUXI_EXT4_INCOMPAT_FILETYPE | FUXI_EXT4_INCOMPAT_EXTENTS;

    /* Bad arguments */
    assert(fuxi_rtc_persist_read_ats(NULL, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_BAD_ARG);
    assert(fuxi_rtc_persist_read_ats(&fs, 0u, mem_read_4k, &disk, &res) == FUXI_EXT4_BAD_ARG);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, NULL, &disk, &res) == FUXI_EXT4_BAD_ARG);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, NULL) == FUXI_EXT4_BAD_ARG);

    /* Bad magic */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le16(img + 1024u + 0x38u, 0xef52u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_NOT_EXT4);

    /* Unsupported block size (8 KiB) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(img + 1024u + 0x18u, 3u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_UNSUPPORTED);

    /* Unsupported incompat features: INLINE_DATA, ENCRYPT, META_BG */
    const uint32_t bad_incompats[] = { 0x8000u, 0x10000u, 0x0010u };
    for (size_t idx = 0; idx < sizeof(bad_incompats) / sizeof(bad_incompats[0]); idx++) {
        build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat | bad_incompats[idx], 1795003732213ull);
        assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_UNSUPPORTED);
    }

    /* Invalid inode_size (64, 192, 8192) */
    const uint16_t bad_isizes[] = { 64u, 192u, 8192u };
    for (size_t idx = 0; idx < sizeof(bad_isizes) / sizeof(bad_isizes[0]); idx++) {
        build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
        put_le16(img + 1024u + 0x58u, bad_isizes[idx]);
        assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_UNSUPPORTED);
    }

    /* blocks_count exceeds partition */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(img + 1024u + 0x04u, total_4k + 1u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Zero blocks_per_group / inodes_per_group */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(img + 1024u + 0x20u, 0u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(img + 1024u + 0x28u, 0u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Directory rec_len == 0 (infinite loop guard) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le16(img + 20u * 4096u + 4u, 0u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Directory unaligned rec_len == 9 */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le16(img + 20u * 4096u + 4u, 9u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Directory name_len exceeding rec_len */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    img[20u * 4096u + 6u] = 200u;
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Extent self-cycle (depth 1 -> block claiming depth 1) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    {
        uint8_t *ib = builder_inode(&bld, 21u) + 0x28u;
        put_le16(ib + 6u, 1u);
        put_le32(ib + 12u, 0u);
        put_le32(ib + 16u, 25u);
        put_le16(ib + 20u, 0u);
        uint8_t *cyc = img + 25u * 4096u;
        put_le16(cyc + 0u, 0xf30au);
        put_le16(cyc + 2u, 1u);
        put_le16(cyc + 4u, 340u);
        put_le16(cyc + 6u, 1u); /* non-decreasing depth -> damaged */
        put_le32(cyc + 12u, 0u);
        put_le32(cyc + 16u, 25u);
    }
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Extent unwritten (ee_len > 32768) */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    builder_set_inode_extent0(&bld, 21u, FUXI_EXT4_S_IFREG | 0600u, 8u, 22u, 32769u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_UNWRITTEN);

    /* Extent physical block out of bounds */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    builder_set_inode_extent0(&bld, 21u, FUXI_EXT4_S_IFREG | 0600u, 8u, total_4k + 5u, 1u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_DAMAGED);

    /* Inode encrypted / inline-data flags */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(builder_inode(&bld, 21u) + 0x20u, FUXI_EXT4_FL_EXTENTS | FUXI_EXT4_FL_ENCRYPT);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_UNSUPPORTED);

    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(builder_inode(&bld, 21u) + 0x20u, FUXI_EXT4_FL_INLINE_DATA);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_UNSUPPORTED);

    /* Wrong types: /time is regfile, ats_2 is symlink */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le16(builder_inode(&bld, 15u) + 0x00u, FUXI_EXT4_S_IFREG | 0600u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_WRONG_TYPE);

    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le16(builder_inode(&bld, 21u) + 0x00u, 0xa000u | 0777u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_WRONG_TYPE);

    /* Bad file sizes (0, 7, 9, 4096) */
    const uint64_t bad_sizes[] = { 0u, 7u, 9u, 4096u };
    for (size_t idx = 0; idx < sizeof(bad_sizes) / sizeof(bad_sizes[0]); idx++) {
        build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
        builder_set_inode_extent0(&bld, 21u, FUXI_EXT4_S_IFREG | 0600u, bad_sizes[idx], 22u, 1u);
        assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_BAD_SIZE);
    }

    /* Missing both ats_2 and ats_1 */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    put_le32(img + 21u * 4096u + 24u, 0u);
    put_le32(img + 21u * 4096u + 40u, 0u);
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_NOT_FOUND);

    /* Read error injected at every I/O step */
    build_standard_persist(&bld, img, total_4k, 4096u, 128u, 32u, std_incompat, 1795003732213ull);
    disk.reads = 0u;
    disk.fail_on_read = 0u;
    assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_OK);
    unsigned total_reads = disk.reads;
    assert(total_reads >= 4u && total_reads <= 12u);
    for (unsigned step = 1u; step <= total_reads; step++) {
        disk.reads = 0u;
        disk.fail_on_read = step;
        assert(fuxi_rtc_persist_read_ats(&fs, total_4k, mem_read_4k, &disk, &res) == FUXI_EXT4_READ_ERROR);
    }
    disk.fail_on_read = 0u;

    assert(disk.oob_reads == 0u);
    free(img);
}

static void
test_selection_policy(void)
{
    struct fuxi_rtc_selection sel;

    /* Default off: ADT wins, persist ignored */
    fuxi_rtc_select_offset(275978u, false, 0u, false, true, 1795003732213ull, "ats_2",
        true, 1794665920u, &sel);
    assert(sel.set_clock && !sel.persist_rejected);
    assert(strcmp(sel.source, "adt") == 0);
    assert(sel.offset_s == 1794665920ull && sel.epoch_s == 1794665920ull + 275978ull && sel.epoch_us == 0u);

    /* Opt-in persist ats_2 overrides stale ADT and keeps millisecond sub-seconds */
    fuxi_rtc_select_offset(275978u, false, 0u, true, true, 1795003732213ull, "ats_2",
        true, 1766678129u, &sel);
    assert(sel.set_clock && !sel.persist_rejected);
    assert(strcmp(sel.source, "persist-ats_2") == 0);
    assert(sel.offset_s == 1795003732ull && sel.epoch_s == 1795279710ull && sel.epoch_us == 213000u);

    /* Opt-in persist ats_2 works with NO ADT fallback present */
    fuxi_rtc_select_offset(275978u, false, 0u, true, true, 1795003732213ull, "ats_2",
        false, 0u, &sel);
    assert(sel.set_clock && !sel.persist_rejected);
    assert(strcmp(sel.source, "persist-ats_2") == 0);
    assert(sel.epoch_s == 1795279710ull && sel.epoch_us == 213000u);

    /* Opt-in persist ats_1 label preserved */
    fuxi_rtc_select_offset(100u, false, 0u, true, true, 1795003732999ull, "ats_1",
        false, 0u, &sel);
    assert(sel.set_clock && strcmp(sel.source, "persist-ats_1") == 0 && sel.epoch_us == 999000u);

    /* Opt-in persist unavailable -> fallback to ADT */
    fuxi_rtc_select_offset(275978u, false, 0u, true, false, 0u, "ats_2",
        true, 1795003732u, &sel);
    assert(sel.set_clock && !sel.persist_rejected && strcmp(sel.source, "adt") == 0);

    /* Opt-in persist bogus zero -> rejected and falls back to ADT */
    fuxi_rtc_select_offset(275978u, false, 0u, true, true, 0ull, "ats_2",
        true, 1795003732u, &sel);
    assert(sel.set_clock && sel.persist_rejected && strcmp(sel.source, "adt") == 0);

    /* Opt-in persist overflow/future -> rejected and falls back to ADT */
    fuxi_rtc_select_offset(275978u, false, 0u, true, true, UINT64_MAX, "ats_2",
        true, 1795003732u, &sel);
    assert(sel.set_clock && sel.persist_rejected && strcmp(sel.source, "adt") == 0);

    /* Explicit boot-arg overrides persist */
    fuxi_rtc_select_offset(100u, true, 1795000000u, true, true, 1795003732213ull, "ats_2",
        true, 1794000000u, &sel);
    assert(sel.set_clock && !sel.persist_rejected && strcmp(sel.source, "boot-arg") == 0);
    assert(sel.offset_s == 1795000000ull && sel.epoch_us == 0u);

    /* Neither persist nor ADT -> clock left alone */
    fuxi_rtc_select_offset(275978u, false, 0u, true, false, 0u, "ats_2",
        false, 0u, &sel);
    assert(!sel.set_clock && strcmp(sel.source, "none") == 0);
}

static int
file_read_4k(void *context, uint64_t lba_4k, void *dst_4k)
{
    FILE *fp = (FILE *)context;
    if (fseek(fp, (long)(lba_4k * 4096ull), SEEK_SET) != 0) {
        return -1;
    }
    return fread(dst_4k, 1u, 4096u, fp) == 4096u ? 0 : -1;
}

static void
test_real_image(const char *spec)
{
    /* Format: expected_ms:expected_dirty:path */
    const char *colon1 = strchr(spec, ':');
    assert(colon1 != NULL);
    const char *colon2 = strchr(colon1 + 1, ':');
    assert(colon2 != NULL);
    uint64_t expected_ms = strtoull(spec, NULL, 10);
    unsigned expected_dirty = (unsigned)strtoul(colon1 + 1, NULL, 10);
    const char *path = colon2 + 1;

    FILE *fp = fopen(path, "rb");
    assert(fp != NULL);
    assert(fseek(fp, 0, SEEK_END) == 0);
    long fsize = ftell(fp);
    assert(fsize > 0 && (fsize % 4096L) == 0);
    uint64_t blocks_4k = (uint64_t)fsize / 4096ull;

    struct fuxi_ext4_ro fs;
    struct fuxi_rtc_persist_result res;
    int rc = fuxi_rtc_persist_read_ats(&fs, blocks_4k, file_read_4k, fp, &res);
    fclose(fp);
    if (rc != FUXI_EXT4_OK || res.offset_ms != expected_ms ||
        (unsigned)res.needs_recovery != expected_dirty ||
        strcmp(res.file_name, "ats_2") != 0) {
        fprintf(stderr, "FAIL real image %s: rc=%d (%s) ms=%llu (exp %llu) dirty=%u (exp %u) file=%s\n",
            path, rc, fuxi_ext4_errstr(rc), (unsigned long long)res.offset_ms,
            (unsigned long long)expected_ms, (unsigned)res.needs_recovery,
            expected_dirty, res.file_name ? res.file_name : "(null)");
        exit(1);
    }
    printf("PASS real persist image %s: /time/%s=%llu ms dirty=%u\n",
        path, res.file_name, (unsigned long long)res.offset_ms, (unsigned)res.needs_recovery);
}

static void
test_rtc_info_abi(void)
{
    assert(sizeof(struct fuxi_rtc_info) == 40u);
    assert(fuxi_rtc_source_code(NULL) == FUXI_RTC_SRC_NONE);
    assert(fuxi_rtc_source_code("none") == FUXI_RTC_SRC_NONE);
    assert(fuxi_rtc_source_code("boot-arg") == FUXI_RTC_SRC_BOOT_ARG);
    assert(fuxi_rtc_source_code("persist-ats_2") == FUXI_RTC_SRC_PERSIST_ATS2);
    assert(fuxi_rtc_source_code("persist-ats_1") == FUXI_RTC_SRC_PERSIST_ATS1);
    assert(fuxi_rtc_source_code("adt") == FUXI_RTC_SRC_ADT);
    assert(fuxi_rtc_source_code("persist-ats_3") == FUXI_RTC_SRC_NONE);

    struct fuxi_rtc_info info;
    memset(&info, 0xaa, sizeof(info));
    fuxi_rtc_fill_info(&info, 280100u, 279881u, 53u, FUXI_RTC_SRC_PERSIST_ATS2,
        FUXI_RTC_FLAG_VALID_APID | FUXI_RTC_FLAG_CLOCK_SET | FUXI_RTC_FLAG_PERSIST_ENABLED,
        1795003732213ull, 1795283613213ull, true);
    assert(info.version == FUXI_RTC_INFO_VERSION);
    assert(info.raw_now == 280100u);
    assert(info.raw_boot == 279881u);
    assert(info.apid == 53u);
    assert(info.boot_source == FUXI_RTC_SRC_PERSIST_ATS2);
    assert((info.flags & FUXI_RTC_FLAG_VALID_APID) != 0);
    assert((info.flags & FUXI_RTC_FLAG_CLOCK_SET) != 0);
    assert((info.flags & FUXI_RTC_FLAG_LIVE_READ) != 0);
    assert(info.boot_offset_ms == 1795003732213ull);
    assert(info.boot_epoch_ms == 1795283613213ull);
}

int
main(int argc, char **argv)
{
    test_synthetic_valid_layouts();
    test_adversarial_and_faults();
    test_selection_policy();
    test_rtc_info_abi();
    for (int idx = 1; idx < argc; idx++) {
        test_real_image(argv[idx]);
    }
    printf("PASS fuxi_rtc_persist host: synthetic layouts, adversarial/faults, selection policy, rtc_info ABI (%d real images)\n",
        argc - 1);
    return 0;
}
'''


def audit_production_sources():
    ufs_text = UFS_C.read_text(encoding='utf-8')
    match = re.search(
        r'int\nfuxi_ufs_persist_find\(.*?\n\}\n\nint fuxi_ufs_persist_read\(.*?\n\}',
        ufs_text, re.DOTALL)
    if match is None:
        raise SystemExit('Could not locate fuxi_ufs_persist_find/read in sweet_ufs.c')
    persist_code = match.group(0)
    for forbidden in ('ufs_write_blocks', 'fx_log_put', '0x2a', 'su.writable', 'su.first_block', 'su.nblocks'):
        if forbidden in persist_code:
            raise SystemExit(f'Forbidden token {forbidden!r} found in persist UFS helper')
    if 'ufs_read_blocks(' not in persist_code or 'fx_gpt_lookup("persist"' not in persist_code:
        raise SystemExit('persist UFS helper missing expected read-only GPT/READ(10) calls')

    keys_text = KEYS_C.read_text(encoding='utf-8')
    for required in ('#include "fuxi_rtc_persist.h"', '"fuxi,rtc-persist"', '"fuxirtcpersist"',
                     '"fuxi,rtc-offset"', 'FUXI RTC: clock set, now=', 'FUXI RTC: persist',
                     'fuxirtc_raw', 'fuxirtc_info', 'fk_sysctl_rtc_raw', 'fk_sysctl_rtc_info'):
        if required not in keys_text:
            raise SystemExit(f'Missing required token {required!r} in fuxi_keys.c')
    rtc_match = re.search(
        r'static bool\nfk_rtc_read_raw\(.*?\nstatic bool\nfk_setup\(',
        keys_text, re.DOTALL)
    if rtc_match is None:
        raise SystemExit('Could not locate RTC block in fuxi_keys.c')
    rtc_code = rtc_match.group(0)
    for forbidden in ('fk_write', '0x6140', '0x6146', '0x40u', '0x46u', 'EXT_WRITEL', 'CTLFLAG_RW', 'CTLFLAG_WR'):
        if forbidden in rtc_code:
            raise SystemExit(f'Forbidden write token {forbidden!r} found in RTC block of fuxi_keys.c')
    for sysctl_name in ('fuxirtc_raw', 'fuxirtc_info'):
        sm = re.search(rf'SYSCTL_PROC\(_kern,\s*OID_AUTO,\s*{sysctl_name},[^\n]+', keys_text)
        if sm is None or 'CTLFLAG_RD' not in sm.group(0) or 'CTLFLAG_WR' in sm.group(0) or 'CTLFLAG_RW' in sm.group(0):
            raise SystemExit(f'Sysctl {sysctl_name} is not strictly CTLFLAG_RD in fuxi_keys.c')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitize', action='store_true', help='Enable ASan and UBSan')
    ap.add_argument('--image', action='append', default=[],
                    help='Real persist.img check in format expected_ms:expected_dirty:path')
    args = ap.parse_args()

    original = HEADER.read_bytes()
    audit_production_sources()
    tools = runpy.run_path(str(ROOT / 'tools/test-fuxi-audio-prep.py'))
    compiler, environment = tools['compiler_environment']()
    msvc = Path(compiler[0].strip('"')).name.lower() in ('cl', 'cl.exe')
    with tempfile.TemporaryDirectory(prefix='fuxi-rtc-persist-host-') as directory:
        tmp = Path(directory)
        src = tmp / 'rtc_persist_host.c'
        src.write_text(HARNESS, encoding='utf-8', newline='\n')
        exe = tmp / ('rtc-persist.exe' if os.name == 'nt' else 'rtc-persist')
        if msvc:
            cmd = compiler + ['/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Od', '/MD',
                              '/I' + str(HEADER.parent), str(src), '/Fe:' + str(exe)]
        else:
            cmd = compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                              '-I' + str(HEADER.parent), str(src), '-o', str(exe)]
            if args.sanitize:
                cmd.insert(-2, '-fsanitize=address,undefined')
        subprocess.run(cmd, cwd=directory, env=environment, check=True, timeout=60)
        result = subprocess.run([str(exe), *args.image], cwd=directory, env=environment,
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        print(result.stdout.strip())
    if HEADER.read_bytes() != original:
        raise SystemExit('Production header changed during host tests.')
    print('sha256 ' + hashlib.sha256(original).hexdigest() + '  ' + str(HEADER))


if __name__ == '__main__':
    main()
