/*
 * Read-only ext4 parser for Android Qualcomm time_daemon offset files
 * (/time/ats_2, fallback /time/ats_1) on the unencrypted LUN 0 "persist"
 * partition, plus RTC offset selection and fallback policy.
 *
 * Reference survey (per AGENTS.md section 0):
 *  - On-disk ext4 superblock, group descriptor, inode, extent tree, and
 *    linear directory entry layout: Linux upstream fs/ext4/ext4.h and
 *    fs/ext4/ext4_extents.h.
 *  - Bounded zero-allocation read-only volume reader pattern over an
 *    untrusted-metadata block callback: modeled on UniXNU-sheng FatVolume.h/.c
 *    (xnu/fuxi-adsp/FatVolume.c). Workspace/UniXNU/ios-fuxi/unixnu-fuxi were
 *    searched first; unixnu-fuxi/drivers/NativeUfs/RootLayout.h only inspects
 *    the 0xef53 superblock magic and has no ext4 inode/extent/dir reader.
 *  - Offset file format (8-byte little-endian uint64_t milliseconds since
 *    Unix epoch at PMIC raw RTC == 0; ATS_USER=ats_2 preferred over
 *    ATS_TOD=ats_1): verified on live Xiaomi 13 Android time_daemon and
 *    2026-11-07 / 2026-11-16 persist.img backups.
 *
 * Strictly read-only, zero heap allocation, self-contained C11 so both XNU
 * (bsd/dev/fuxi_keys.c) and host ASan/UBSan tests compile the exact same code.
 */
#ifndef FUXI_RTC_PERSIST_H
#define FUXI_RTC_PERSIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FUXI_EXT4_DEV_BLOCK         4096u
#define FUXI_EXT4_ROOT_INO          2u
#define FUXI_EXT4_MAGIC             0xef53u
#define FUXI_EXT4_EXT_MAGIC         0xf30au
#define FUXI_EXT4_MAX_EXTENT_DEPTH  5u
#define FUXI_EXT4_MAX_DIR_BLOCKS    64u

#define FUXI_EXT4_S_IFMT            0xf000u
#define FUXI_EXT4_S_IFDIR           0x4000u
#define FUXI_EXT4_S_IFREG           0x8000u

#define FUXI_EXT4_INCOMPAT_FILETYPE 0x0002u
#define FUXI_EXT4_INCOMPAT_RECOVER  0x0004u
#define FUXI_EXT4_INCOMPAT_EXTENTS  0x0040u
#define FUXI_EXT4_INCOMPAT_64BIT    0x0080u
#define FUXI_EXT4_INCOMPAT_FLEX_BG  0x0200u
#define FUXI_EXT4_INCOMPAT_CSUMSEED 0x2000u
#define FUXI_EXT4_INCOMPAT_LARGEDIR 0x4000u
#define FUXI_EXT4_INCOMPAT_ALLOWED  (FUXI_EXT4_INCOMPAT_FILETYPE | \
                                     FUXI_EXT4_INCOMPAT_RECOVER  | \
                                     FUXI_EXT4_INCOMPAT_EXTENTS  | \
                                     FUXI_EXT4_INCOMPAT_64BIT    | \
                                     FUXI_EXT4_INCOMPAT_FLEX_BG  | \
                                     FUXI_EXT4_INCOMPAT_CSUMSEED | \
                                     FUXI_EXT4_INCOMPAT_LARGEDIR)

#define FUXI_EXT4_FL_ENCRYPT        0x00000800u
#define FUXI_EXT4_FL_EXTENTS        0x00080000u
#define FUXI_EXT4_FL_INLINE_DATA    0x10000000u

/* 2023-11-14 .. 2039-09-19 UTC: same sanity window as existing fuxi_keys.c. */
#define FUXI_RTC_EPOCH_MIN          1700000000ull
#define FUXI_RTC_EPOCH_MAX          2200000000ull

enum fuxi_ext4_status {
	FUXI_EXT4_OK = 0,
	FUXI_EXT4_BAD_ARG = -1,
	FUXI_EXT4_READ_ERROR = -2,
	FUXI_EXT4_NOT_EXT4 = -3,
	FUXI_EXT4_UNSUPPORTED = -4,
	FUXI_EXT4_DAMAGED = -5,
	FUXI_EXT4_NOT_FOUND = -6,
	FUXI_EXT4_WRONG_TYPE = -7,
	FUXI_EXT4_BAD_SIZE = -8,
	FUXI_EXT4_UNWRITTEN = -9,
	FUXI_EXT4_TOO_LARGE = -10,
};

/* Reads one 4096-byte partition-relative block at lba_4k into dst_4k; returns 0 on success. */
typedef int (*fuxi_ext4_read_4k_fn)(void *context, uint64_t lba_4k, void *dst_4k);

struct fuxi_ext4_ro {
	fuxi_ext4_read_4k_fn read_4k;
	void *ctx;
	uint64_t part_blocks_4k;
	uint64_t blocks_count;
	uint64_t groups_count;
	uint32_t block_size;
	uint32_t first_data_block;
	uint32_t blocks_per_group;
	uint32_t inodes_count;
	uint32_t inodes_per_group;
	uint32_t inode_size;
	uint32_t desc_size;
	uint32_t gdt_block;
	uint32_t incompat;
	bool needs_recovery;
	uint64_t cached_lba;
	uint8_t buf[FUXI_EXT4_DEV_BLOCK];
};

struct fuxi_ext4_inode {
	uint16_t mode;
	uint32_t flags;
	uint64_t size;
	uint8_t block[60];
};

struct fuxi_rtc_persist_result {
	uint64_t offset_ms;
	const char *file_name;
	bool needs_recovery;
};

struct fuxi_rtc_selection {
	uint64_t offset_s;
	uint64_t epoch_s;
	uint32_t epoch_us;
	const char *source;
	bool set_clock;
	bool persist_rejected;
};

#define FUXI_RTC_INFO_VERSION        1u

#define FUXI_RTC_SRC_NONE            0u
#define FUXI_RTC_SRC_BOOT_ARG        1u
#define FUXI_RTC_SRC_PERSIST_ATS2    2u
#define FUXI_RTC_SRC_PERSIST_ATS1    3u
#define FUXI_RTC_SRC_ADT             4u

#define FUXI_RTC_FLAG_VALID_APID       (1u << 0)
#define FUXI_RTC_FLAG_CLOCK_SET        (1u << 1)
#define FUXI_RTC_FLAG_PERSIST_ENABLED  (1u << 2)
#define FUXI_RTC_FLAG_PERSIST_REJECTED (1u << 3)
#define FUXI_RTC_FLAG_LIVE_READ        (1u << 4)

/*
 * Read-only sysctl ABI for kern.fuxirtc_info (40 bytes, little-endian).
 * Mirrored in userland FuxiRtcPersistLogic.h with matching _Static_assert.
 */
struct fuxi_rtc_info {
	uint32_t version;
	uint32_t flags;
	uint32_t raw_now;
	uint32_t raw_boot;
	uint32_t boot_source;
	uint32_t apid;
	uint64_t boot_offset_ms;
	uint64_t boot_epoch_ms;
};
_Static_assert(sizeof(struct fuxi_rtc_info) == 40, "fuxi_rtc_info ABI must be 40 bytes");

static inline uint16_t
fuxi_ext4_le16(const uint8_t *ptr)
{
	return (uint16_t)((uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8));
}

static inline uint32_t
fuxi_ext4_le32(const uint8_t *ptr)
{
	return (uint32_t)fuxi_ext4_le16(ptr) |
	    ((uint32_t)fuxi_ext4_le16(ptr + 2) << 16);
}

static inline uint64_t
fuxi_ext4_le64(const uint8_t *ptr)
{
	return (uint64_t)fuxi_ext4_le32(ptr) |
	    ((uint64_t)fuxi_ext4_le32(ptr + 4) << 32);
}

static inline bool
fuxi_ext4_is_pow2(uint32_t value)
{
	return value != 0u && (value & (value - 1u)) == 0u;
}

static inline bool
fuxi_ext4_name_eq(const uint8_t *raw, uint32_t raw_len, const char *want)
{
	uint32_t idx = 0u;
	while (want[idx] != '\0') {
		if (idx >= raw_len || raw[idx] != (uint8_t)want[idx]) {
			return false;
		}
		idx++;
	}
	return idx == raw_len;
}

static inline const char *
fuxi_ext4_errstr(int status)
{
	switch (status) {
	case FUXI_EXT4_OK: return "ok";
	case FUXI_EXT4_BAD_ARG: return "bad-arg";
	case FUXI_EXT4_READ_ERROR: return "read-error";
	case FUXI_EXT4_NOT_EXT4: return "not-ext4";
	case FUXI_EXT4_UNSUPPORTED: return "unsupported-feature";
	case FUXI_EXT4_DAMAGED: return "damaged-metadata";
	case FUXI_EXT4_NOT_FOUND: return "not-found";
	case FUXI_EXT4_WRONG_TYPE: return "wrong-type";
	case FUXI_EXT4_BAD_SIZE: return "bad-size";
	case FUXI_EXT4_UNWRITTEN: return "unwritten-extent";
	case FUXI_EXT4_TOO_LARGE: return "dir-too-large";
	default: return "unknown";
	}
}

static int
fuxi_ext4_fetch_4k(struct fuxi_ext4_ro *fs, uint64_t lba_4k)
{
	if (lba_4k >= fs->part_blocks_4k) {
		return FUXI_EXT4_DAMAGED;
	}
	if (fs->cached_lba == lba_4k) {
		return FUXI_EXT4_OK;
	}
	fs->cached_lba = UINT64_MAX;
	if (fs->read_4k(fs->ctx, lba_4k, fs->buf) != 0) {
		return FUXI_EXT4_READ_ERROR;
	}
	fs->cached_lba = lba_4k;
	return FUXI_EXT4_OK;
}

static int
fuxi_ext4_fetch_fsblk(struct fuxi_ext4_ro *fs, uint64_t fs_blk, const uint8_t **out_ptr)
{
	if (fs_blk >= fs->blocks_count) {
		return FUXI_EXT4_DAMAGED;
	}
	uint32_t per_4k = FUXI_EXT4_DEV_BLOCK / fs->block_size;
	uint64_t lba_4k = fs_blk / per_4k;
	uint32_t slot = (uint32_t)(fs_blk % per_4k);
	int status = fuxi_ext4_fetch_4k(fs, lba_4k);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	*out_ptr = fs->buf + slot * fs->block_size;
	return FUXI_EXT4_OK;
}

static int
fuxi_ext4_mount(struct fuxi_ext4_ro *fs, uint64_t part_blocks_4k,
    fuxi_ext4_read_4k_fn read_4k, void *ctx)
{
	if (!fs || !read_4k || part_blocks_4k == 0u) {
		return FUXI_EXT4_BAD_ARG;
	}
	uint8_t *zero_ptr = (uint8_t *)fs;
	for (size_t idx = 0; idx < sizeof(*fs); idx++) {
		zero_ptr[idx] = 0;
	}
	fs->read_4k = read_4k;
	fs->ctx = ctx;
	fs->part_blocks_4k = part_blocks_4k;
	fs->cached_lba = UINT64_MAX;

	int status = fuxi_ext4_fetch_4k(fs, 0u);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	const uint8_t *sb = fs->buf + 1024u;
	if (fuxi_ext4_le16(sb + 0x38u) != FUXI_EXT4_MAGIC) {
		return FUXI_EXT4_NOT_EXT4;
	}
	uint32_t log_bsize = fuxi_ext4_le32(sb + 0x18u);
	if (log_bsize > 2u) {
		return FUXI_EXT4_UNSUPPORTED;
	}
	uint32_t block_size = 1024u << log_bsize;
	uint32_t rev_level = fuxi_ext4_le32(sb + 0x4cu);
	uint32_t incompat = 0u;
	uint32_t inode_size = 128u;
	uint32_t desc_size = 32u;
	if (rev_level == 1u) {
		incompat = fuxi_ext4_le32(sb + 0x60u);
		if ((incompat & ~FUXI_EXT4_INCOMPAT_ALLOWED) != 0u) {
			return FUXI_EXT4_UNSUPPORTED;
		}
		inode_size = fuxi_ext4_le16(sb + 0x58u);
		if (inode_size < 128u || inode_size > block_size || !fuxi_ext4_is_pow2(inode_size)) {
			return FUXI_EXT4_UNSUPPORTED;
		}
		uint32_t raw_desc = fuxi_ext4_le16(sb + 0xfeu);
		if ((incompat & FUXI_EXT4_INCOMPAT_64BIT) != 0u) {
			if (raw_desc < 64u || raw_desc > block_size || !fuxi_ext4_is_pow2(raw_desc)) {
				return FUXI_EXT4_UNSUPPORTED;
			}
			desc_size = raw_desc;
		} else {
			if (raw_desc != 0u && raw_desc != 32u) {
				return FUXI_EXT4_UNSUPPORTED;
			}
			desc_size = 32u;
		}
	} else if (rev_level != 0u) {
		return FUXI_EXT4_UNSUPPORTED;
	}

	uint32_t inodes_count = fuxi_ext4_le32(sb + 0x00u);
	uint64_t blocks_count = (uint64_t)fuxi_ext4_le32(sb + 0x04u);
	if ((incompat & FUXI_EXT4_INCOMPAT_64BIT) != 0u) {
		blocks_count |= (uint64_t)fuxi_ext4_le32(sb + 0x150u) << 32;
	}
	uint32_t first_data_block = fuxi_ext4_le32(sb + 0x14u);
	uint32_t blocks_per_group = fuxi_ext4_le32(sb + 0x20u);
	uint32_t inodes_per_group = fuxi_ext4_le32(sb + 0x28u);
	uint32_t expected_first = (block_size == 1024u) ? 1u : 0u;
	if (first_data_block != expected_first) {
		return FUXI_EXT4_DAMAGED;
	}
	if (inodes_count < FUXI_EXT4_ROOT_INO || blocks_per_group == 0u ||
	    inodes_per_group == 0u || inodes_per_group > inodes_count) {
		return FUXI_EXT4_DAMAGED;
	}
	uint32_t per_4k = FUXI_EXT4_DEV_BLOCK / block_size;
	if (part_blocks_4k > UINT64_MAX / per_4k) {
		return FUXI_EXT4_DAMAGED;
	}
	uint64_t max_fs_blocks = part_blocks_4k * per_4k;
	if (blocks_count <= (uint64_t)first_data_block + 1ull || blocks_count > max_fs_blocks) {
		return FUXI_EXT4_DAMAGED;
	}
	uint64_t data_blocks = blocks_count - first_data_block;
	uint64_t groups_count = (data_blocks - 1ull) / (uint64_t)blocks_per_group + 1ull;
	if (groups_count == 0ull || groups_count > 0xffffffffull) {
		return FUXI_EXT4_DAMAGED;
	}
	if ((uint64_t)inodes_count > groups_count * (uint64_t)inodes_per_group) {
		return FUXI_EXT4_DAMAGED;
	}
	uint32_t gdt_block = (block_size == 1024u) ? 2u : 1u;
	uint64_t gdt_blocks_needed = (groups_count * (uint64_t)desc_size + block_size - 1ull) / block_size;
	if ((uint64_t)gdt_block >= blocks_count || gdt_blocks_needed > blocks_count - gdt_block) {
		return FUXI_EXT4_DAMAGED;
	}

	fs->blocks_count = blocks_count;
	fs->groups_count = groups_count;
	fs->block_size = block_size;
	fs->first_data_block = first_data_block;
	fs->blocks_per_group = blocks_per_group;
	fs->inodes_count = inodes_count;
	fs->inodes_per_group = inodes_per_group;
	fs->inode_size = inode_size;
	fs->desc_size = desc_size;
	fs->gdt_block = gdt_block;
	fs->incompat = incompat;
	fs->needs_recovery = (incompat & FUXI_EXT4_INCOMPAT_RECOVER) != 0u;
	return FUXI_EXT4_OK;
}

static int
fuxi_ext4_read_inode(struct fuxi_ext4_ro *fs, uint32_t ino, struct fuxi_ext4_inode *out_inode)
{
	if (ino == 0u || ino > fs->inodes_count) {
		return FUXI_EXT4_DAMAGED;
	}
	uint32_t group = (ino - 1u) / fs->inodes_per_group;
	uint32_t index = (ino - 1u) % fs->inodes_per_group;
	if ((uint64_t)group >= fs->groups_count) {
		return FUXI_EXT4_DAMAGED;
	}

	uint64_t gdt_byte_off = (uint64_t)fs->gdt_block * (uint64_t)fs->block_size +
	    (uint64_t)group * (uint64_t)fs->desc_size;
	uint64_t gdt_lba = gdt_byte_off / FUXI_EXT4_DEV_BLOCK;
	uint32_t gdt_within = (uint32_t)(gdt_byte_off % FUXI_EXT4_DEV_BLOCK);
	if (gdt_within + fs->desc_size > FUXI_EXT4_DEV_BLOCK) {
		return FUXI_EXT4_DAMAGED;
	}
	int status = fuxi_ext4_fetch_4k(fs, gdt_lba);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	const uint8_t *bgd = fs->buf + gdt_within;
	uint64_t itable_blk = (uint64_t)fuxi_ext4_le32(bgd + 8u);
	if ((fs->incompat & FUXI_EXT4_INCOMPAT_64BIT) != 0u && fs->desc_size >= 64u) {
		itable_blk |= (uint64_t)fuxi_ext4_le32(bgd + 40u) << 32;
	}
	if (itable_blk >= fs->blocks_count) {
		return FUXI_EXT4_DAMAGED;
	}
	uint64_t inodes_per_blk = fs->block_size / fs->inode_size;
	uint64_t blk_delta = (uint64_t)index / inodes_per_blk;
	uint32_t ino_slot = (uint32_t)((uint64_t)index % inodes_per_blk);
	if (blk_delta >= fs->blocks_count - itable_blk) {
		return FUXI_EXT4_DAMAGED;
	}
	const uint8_t *blk_ptr = NULL;
	status = fuxi_ext4_fetch_fsblk(fs, itable_blk + blk_delta, &blk_ptr);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	const uint8_t *raw = blk_ptr + ino_slot * fs->inode_size;
	out_inode->mode = fuxi_ext4_le16(raw + 0x00u);
	out_inode->flags = fuxi_ext4_le32(raw + 0x20u);
	uint32_t size_lo = fuxi_ext4_le32(raw + 0x04u);
	uint32_t size_hi = fuxi_ext4_le32(raw + 0x6cu);
	uint16_t ftype = out_inode->mode & FUXI_EXT4_S_IFMT;
	if (ftype == FUXI_EXT4_S_IFREG ||
	    (ftype == FUXI_EXT4_S_IFDIR && (fs->incompat & FUXI_EXT4_INCOMPAT_LARGEDIR) != 0u)) {
		out_inode->size = (uint64_t)size_lo | ((uint64_t)size_hi << 32);
	} else {
		out_inode->size = (uint64_t)size_lo;
	}
	for (size_t idx = 0; idx < sizeof(out_inode->block); idx++) {
		out_inode->block[idx] = raw[0x28u + idx];
	}
	return FUXI_EXT4_OK;
}

static int
fuxi_ext4_map_block(struct fuxi_ext4_ro *fs, const struct fuxi_ext4_inode *inode,
    uint32_t logical_blk, uint64_t *out_phys)
{
	if ((inode->flags & (FUXI_EXT4_FL_ENCRYPT | FUXI_EXT4_FL_INLINE_DATA)) != 0u) {
		return FUXI_EXT4_UNSUPPORTED;
	}
	if ((inode->flags & FUXI_EXT4_FL_EXTENTS) != 0u) {
		const uint8_t *node_ptr = inode->block;
		uint32_t node_bytes = (uint32_t)sizeof(inode->block);
		uint16_t prev_depth = 0xffffu;

		for (unsigned step = 0u; step <= FUXI_EXT4_MAX_EXTENT_DEPTH; step++) {
			if (node_bytes < 12u) {
				return FUXI_EXT4_DAMAGED;
			}
			uint16_t eh_magic = fuxi_ext4_le16(node_ptr + 0u);
			uint16_t eh_entries = fuxi_ext4_le16(node_ptr + 2u);
			uint16_t eh_max = fuxi_ext4_le16(node_ptr + 4u);
			uint16_t eh_depth = fuxi_ext4_le16(node_ptr + 6u);
			if (eh_magic != FUXI_EXT4_EXT_MAGIC || eh_depth > FUXI_EXT4_MAX_EXTENT_DEPTH) {
				return FUXI_EXT4_DAMAGED;
			}
			if (step > 0u && eh_depth != prev_depth - 1u) {
				return FUXI_EXT4_DAMAGED;
			}
			uint32_t max_fit = (node_bytes - 12u) / 12u;
			if (eh_max > max_fit || eh_entries > eh_max) {
				return FUXI_EXT4_DAMAGED;
			}
			if (eh_entries == 0u) {
				return FUXI_EXT4_NOT_FOUND;
			}
			if (eh_depth == 0u) {
				for (uint16_t idx = 0u; idx < eh_entries; idx++) {
					const uint8_t *ent = node_ptr + 12u + (uint32_t)idx * 12u;
					uint32_t ee_block = fuxi_ext4_le32(ent + 0u);
					uint16_t ee_len = fuxi_ext4_le16(ent + 4u);
					uint16_t ee_start_hi = fuxi_ext4_le16(ent + 6u);
					uint32_t ee_start_lo = fuxi_ext4_le32(ent + 8u);
					bool unwritten = ee_len > 32768u;
					uint32_t span = unwritten ? (uint32_t)(ee_len - 32768u) : (uint32_t)ee_len;
					if (span == 0u) {
						return FUXI_EXT4_DAMAGED;
					}
					if (logical_blk >= ee_block && logical_blk - ee_block < span) {
						if (unwritten) {
							return FUXI_EXT4_UNWRITTEN;
						}
						uint64_t base = ((uint64_t)ee_start_hi << 32) | (uint64_t)ee_start_lo;
						uint64_t delta = (uint64_t)(logical_blk - ee_block);
						if (base >= fs->blocks_count || delta >= fs->blocks_count - base) {
							return FUXI_EXT4_DAMAGED;
						}
						*out_phys = base + delta;
						return FUXI_EXT4_OK;
					}
				}
				return FUXI_EXT4_NOT_FOUND;
			}
			bool chosen = false;
			uint64_t child_blk = 0u;
			uint32_t prev_ei_block = 0u;
			for (uint16_t idx = 0u; idx < eh_entries; idx++) {
				const uint8_t *ent = node_ptr + 12u + (uint32_t)idx * 12u;
				uint32_t ei_block = fuxi_ext4_le32(ent + 0u);
				uint32_t ei_leaf_lo = fuxi_ext4_le32(ent + 4u);
				uint16_t ei_leaf_hi = fuxi_ext4_le16(ent + 8u);
				if (idx > 0u && ei_block <= prev_ei_block) {
					return FUXI_EXT4_DAMAGED;
				}
				prev_ei_block = ei_block;
				if (ei_block <= logical_blk) {
					child_blk = ((uint64_t)ei_leaf_hi << 32) | (uint64_t)ei_leaf_lo;
					chosen = true;
				} else {
					break;
				}
			}
			if (!chosen) {
				return FUXI_EXT4_NOT_FOUND;
			}
			prev_depth = eh_depth;
			int status = fuxi_ext4_fetch_fsblk(fs, child_blk, &node_ptr);
			if (status != FUXI_EXT4_OK) {
				return status;
			}
			node_bytes = fs->block_size;
		}
		return FUXI_EXT4_DAMAGED;
	}

	if (logical_blk < 12u) {
		uint32_t direct = fuxi_ext4_le32(inode->block + logical_blk * 4u);
		if (direct == 0u) {
			return FUXI_EXT4_NOT_FOUND;
		}
		if ((uint64_t)direct >= fs->blocks_count) {
			return FUXI_EXT4_DAMAGED;
		}
		*out_phys = (uint64_t)direct;
		return FUXI_EXT4_OK;
	}
	uint32_t ptrs_per_blk = fs->block_size / 4u;
	if (logical_blk - 12u < ptrs_per_blk) {
		uint32_t ind_blk = fuxi_ext4_le32(inode->block + 12u * 4u);
		if (ind_blk == 0u) {
			return FUXI_EXT4_NOT_FOUND;
		}
		const uint8_t *blk_ptr = NULL;
		int status = fuxi_ext4_fetch_fsblk(fs, (uint64_t)ind_blk, &blk_ptr);
		if (status != FUXI_EXT4_OK) {
			return status;
		}
		uint32_t phys = fuxi_ext4_le32(blk_ptr + (logical_blk - 12u) * 4u);
		if (phys == 0u) {
			return FUXI_EXT4_NOT_FOUND;
		}
		if ((uint64_t)phys >= fs->blocks_count) {
			return FUXI_EXT4_DAMAGED;
		}
		*out_phys = (uint64_t)phys;
		return FUXI_EXT4_OK;
	}
	return FUXI_EXT4_UNSUPPORTED;
}

static int
fuxi_ext4_lookup_dir(struct fuxi_ext4_ro *fs, const struct fuxi_ext4_inode *dir_inode,
    const char *target, uint32_t *out_ino)
{
	if ((dir_inode->mode & FUXI_EXT4_S_IFMT) != FUXI_EXT4_S_IFDIR) {
		return FUXI_EXT4_WRONG_TYPE;
	}
	if (dir_inode->size == 0u) {
		return FUXI_EXT4_DAMAGED;
	}
	uint64_t dir_blocks = (dir_inode->size - 1ull) / fs->block_size + 1ull;
	if (dir_blocks > FUXI_EXT4_MAX_DIR_BLOCKS) {
		return FUXI_EXT4_TOO_LARGE;
	}

	for (uint32_t blk_idx = 0u; blk_idx < (uint32_t)dir_blocks; blk_idx++) {
		uint64_t phys = 0u;
		int status = fuxi_ext4_map_block(fs, dir_inode, blk_idx, &phys);
		if (status == FUXI_EXT4_NOT_FOUND) {
			continue;
		}
		if (status != FUXI_EXT4_OK) {
			return status;
		}
		const uint8_t *blk_ptr = NULL;
		status = fuxi_ext4_fetch_fsblk(fs, phys, &blk_ptr);
		if (status != FUXI_EXT4_OK) {
			return status;
		}
		uint32_t pos = 0u;
		while (pos + 8u <= fs->block_size) {
			uint32_t de_ino = fuxi_ext4_le32(blk_ptr + pos);
			uint16_t rec_len = fuxi_ext4_le16(blk_ptr + pos + 4u);
			uint8_t name_len = blk_ptr[pos + 6u];
			if (rec_len < 8u || (rec_len & 3u) != 0u ||
			    (uint32_t)rec_len > fs->block_size - pos) {
				return FUXI_EXT4_DAMAGED;
			}
			if (de_ino != 0u) {
				if (8u + (uint32_t)name_len > (uint32_t)rec_len) {
					return FUXI_EXT4_DAMAGED;
				}
				if (fuxi_ext4_name_eq(blk_ptr + pos + 8u, name_len, target)) {
					*out_ino = de_ino;
					return FUXI_EXT4_OK;
				}
			}
			pos += rec_len;
		}
	}
	return FUXI_EXT4_NOT_FOUND;
}

static int
fuxi_ext4_read_u64_file(struct fuxi_ext4_ro *fs, uint32_t file_ino, uint64_t *out_value)
{
	struct fuxi_ext4_inode file_inode;
	int status = fuxi_ext4_read_inode(fs, file_ino, &file_inode);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	if ((file_inode.mode & FUXI_EXT4_S_IFMT) != FUXI_EXT4_S_IFREG) {
		return FUXI_EXT4_WRONG_TYPE;
	}
	if (file_inode.size != sizeof(uint64_t)) {
		return FUXI_EXT4_BAD_SIZE;
	}
	uint64_t phys = 0u;
	status = fuxi_ext4_map_block(fs, &file_inode, 0u, &phys);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	const uint8_t *blk_ptr = NULL;
	status = fuxi_ext4_fetch_fsblk(fs, phys, &blk_ptr);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	*out_value = fuxi_ext4_le64(blk_ptr);
	return FUXI_EXT4_OK;
}

/*
 * Reads Android Qualcomm time_daemon's /time/ats_2 (fallback /time/ats_1 if
 * ats_2 is absent) from a read-only ext4 persist volume.
 */
static int
fuxi_rtc_persist_read_ats(struct fuxi_ext4_ro *fs, uint64_t part_blocks_4k,
    fuxi_ext4_read_4k_fn read_4k, void *ctx, struct fuxi_rtc_persist_result *out_res)
{
	if (!out_res) {
		return FUXI_EXT4_BAD_ARG;
	}
	out_res->offset_ms = 0u;
	out_res->file_name = "ats_2";
	out_res->needs_recovery = false;

	int status = fuxi_ext4_mount(fs, part_blocks_4k, read_4k, ctx);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	out_res->needs_recovery = fs->needs_recovery;

	struct fuxi_ext4_inode root_inode;
	status = fuxi_ext4_read_inode(fs, FUXI_EXT4_ROOT_INO, &root_inode);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	uint32_t time_ino = 0u;
	status = fuxi_ext4_lookup_dir(fs, &root_inode, "time", &time_ino);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	struct fuxi_ext4_inode time_inode;
	status = fuxi_ext4_read_inode(fs, time_ino, &time_inode);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	uint32_t ats_ino = 0u;
	const char *picked = "ats_2";
	status = fuxi_ext4_lookup_dir(fs, &time_inode, "ats_2", &ats_ino);
	if (status == FUXI_EXT4_NOT_FOUND) {
		picked = "ats_1";
		status = fuxi_ext4_lookup_dir(fs, &time_inode, "ats_1", &ats_ino);
	}
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	uint64_t offset_ms = 0u;
	status = fuxi_ext4_read_u64_file(fs, ats_ino, &offset_ms);
	if (status != FUXI_EXT4_OK) {
		return status;
	}
	out_res->offset_ms = offset_ms;
	out_res->file_name = picked;
	return FUXI_EXT4_OK;
}

static inline bool
fuxi_rtc_epoch_sane(uint64_t epoch_s)
{
	return epoch_s >= FUXI_RTC_EPOCH_MIN && epoch_s <= FUXI_RTC_EPOCH_MAX;
}

/*
 * Selects the boot-time RTC offset source with fallback:
 *   1. Explicit fuxirtcoffset= boot-arg (debug override).
 *   2. Opt-in persist:/time/ats_2 (or ats_1), if valid and within the sanity window.
 *   3. ADT /chosen fuxi,rtc-offset fallback.
 *   4. Unoffset raw counter (rejected unless already within the sanity window).
 */
static void
fuxi_rtc_select_offset(uint32_t raw_secs,
    bool has_boot_arg, uint32_t boot_arg_offset,
    bool persist_enabled, bool persist_ok, uint64_t persist_ms, const char *persist_file,
    bool has_adt, uint32_t adt_offset,
    struct fuxi_rtc_selection *out_sel)
{
	out_sel->offset_s = 0u;
	out_sel->epoch_s = (uint64_t)raw_secs;
	out_sel->epoch_us = 0u;
	out_sel->source = "none";
	out_sel->set_clock = false;
	out_sel->persist_rejected = false;

	if (has_boot_arg) {
		out_sel->offset_s = (uint64_t)boot_arg_offset;
		out_sel->epoch_s = (uint64_t)raw_secs + (uint64_t)boot_arg_offset;
		out_sel->source = "boot-arg";
		out_sel->set_clock = fuxi_rtc_epoch_sane(out_sel->epoch_s);
		return;
	}

	if (persist_enabled && persist_ok) {
		uint64_t p_off_s = persist_ms / 1000ull;
		uint32_t p_off_us = (uint32_t)((persist_ms % 1000ull) * 1000ull);
		if (p_off_s <= UINT64_MAX - (uint64_t)raw_secs) {
			uint64_t p_epoch_s = (uint64_t)raw_secs + p_off_s;
			if (fuxi_rtc_epoch_sane(p_epoch_s)) {
				out_sel->offset_s = p_off_s;
				out_sel->epoch_s = p_epoch_s;
				out_sel->epoch_us = p_off_us;
				out_sel->source = (persist_file && persist_file[0] == 'a' &&
				    persist_file[1] == 't' && persist_file[2] == 's' &&
				    persist_file[3] == '_' && persist_file[4] == '1' &&
				    persist_file[5] == '\0') ? "persist-ats_1" : "persist-ats_2";
				out_sel->set_clock = true;
				return;
			}
		}
		out_sel->persist_rejected = true;
	}

	if (has_adt) {
		out_sel->offset_s = (uint64_t)adt_offset;
		out_sel->epoch_s = (uint64_t)raw_secs + (uint64_t)adt_offset;
		out_sel->source = "adt";
	}
	out_sel->set_clock = fuxi_rtc_epoch_sane(out_sel->epoch_s);
}

static inline uint32_t
fuxi_rtc_source_code(const char *source)
{
	if (!source) {
		return FUXI_RTC_SRC_NONE;
	}
	if (source[0] == 'b' && source[1] == 'o' && source[2] == 'o' && source[3] == 't' &&
	    source[4] == '-' && source[5] == 'a' && source[6] == 'r' && source[7] == 'g' &&
	    source[8] == '\0') {
		return FUXI_RTC_SRC_BOOT_ARG;
	}
	if (source[0] == 'p' && source[1] == 'e' && source[2] == 'r' && source[3] == 's' &&
	    source[4] == 'i' && source[5] == 's' && source[6] == 't' && source[7] == '-' &&
	    source[8] == 'a' && source[9] == 't' && source[10] == 's' && source[11] == '_' &&
	    source[13] == '\0') {
		if (source[12] == '2') {
			return FUXI_RTC_SRC_PERSIST_ATS2;
		}
		if (source[12] == '1') {
			return FUXI_RTC_SRC_PERSIST_ATS1;
		}
	}
	if (source[0] == 'a' && source[1] == 'd' && source[2] == 't' && source[3] == '\0') {
		return FUXI_RTC_SRC_ADT;
	}
	return FUXI_RTC_SRC_NONE;
}

static inline void
fuxi_rtc_fill_info(struct fuxi_rtc_info *out,
    uint32_t raw_now, uint32_t raw_boot, uint32_t apid,
    uint32_t boot_source, uint32_t boot_flags,
    uint64_t boot_offset_ms, uint64_t boot_epoch_ms,
    bool live_read)
{
	memset(out, 0, sizeof(*out));
	out->version = FUXI_RTC_INFO_VERSION;
	out->flags = boot_flags | (live_read ? FUXI_RTC_FLAG_LIVE_READ : 0u);
	out->raw_now = raw_now;
	out->raw_boot = raw_boot;
	out->boot_source = boot_source;
	out->apid = apid;
	out->boot_offset_ms = boot_offset_ms;
	out->boot_epoch_ms = boot_epoch_ms;
}

#endif /* FUXI_RTC_PERSIST_H */
