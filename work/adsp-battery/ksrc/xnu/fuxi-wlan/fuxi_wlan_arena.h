#ifndef FUXI_WLAN_ARENA_H
#define FUXI_WLAN_ARENA_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct fwa_block {
	uint32_t offset, size, requested;
};
struct fwa_arena {
	uint8_t *memory;
	uint64_t bus;
	uint32_t bytes, count, capacity;
	struct fwa_block *blocks;
};

static int
fwa_init(struct fwa_arena *arena, void *memory, uint64_t bus, uint32_t bytes,
    struct fwa_block *blocks, uint32_t capacity)
{
	if (!arena || !memory || !blocks || capacity < 2 || bytes <= 4096 ||
	    ((uintptr_t)memory & 63) || (bus & 4095) || bytes > 0xffffffffu - 4095 ||
	    bus > 0xffffffffu || bytes > UINT64_C(0x100000000) - bus) { return 1; }
	memset(arena, 0, sizeof(*arena));
	arena->memory = memory;
	arena->bus = bus;
	arena->bytes = bytes;
	arena->blocks = blocks;
	arena->capacity = capacity;
	arena->count = 2;
	blocks[0] = (struct fwa_block){0, 4096, 4096};
	blocks[1] = (struct fwa_block){4096, bytes - 4096, 0};
	return 0;
}

static void *
fwa_alloc(struct fwa_arena *arena, size_t bytes, uint64_t *bus)
{
	if (bus) { *bus = 0; }
	if (!arena || !arena->memory || !bytes || bytes > 0xffffffffu - 4095) { return NULL; }
	uint32_t rounded = ((uint32_t)bytes + 63) & ~63u, alignment = 64;
	if (rounded >= 4096) {
		rounded = (rounded + 4095) & ~4095u;
		alignment = 4096;
		while (alignment < rounded && alignment < 0x80000000u) { alignment <<= 1; }
		if (alignment < rounded) { return NULL; }
	} else {
		/* Match Linux SLUB/dma_pool natural alignment so sub-page TX/ext_desc buffers
		 * never straddle a 4 KiB PCIe boundary and satisfy WCN7850 128-byte IOVA alignment. */
		while (alignment < rounded && alignment < 4096) { alignment <<= 1; }
	}
	for (uint32_t index = 1; index < arena->count; index++) {
		struct fwa_block whole = arena->blocks[index];
		if (whole.requested) { continue; }
		uint64_t start_bus = arena->bus + whole.offset;
		uint64_t aligned_bus = (start_bus + alignment - 1) & ~((uint64_t)alignment - 1);
		uint64_t padding = aligned_bus - start_bus;
		if (padding > whole.size || rounded > whole.size - padding) { continue; }
		uint32_t start = whole.offset + (uint32_t)padding;
		uint32_t tail = whole.size - (uint32_t)padding - rounded;
		uint32_t extra = (padding ? 1u : 0u) + (tail ? 1u : 0u);
		if (extra > arena->capacity - arena->count) { continue; }
		memmove(&arena->blocks[index + extra], &arena->blocks[index],
		    (arena->count - index) * sizeof(*arena->blocks));
		arena->count += extra;
		uint32_t at = index;
		if (padding) { arena->blocks[at++] = (struct fwa_block){whole.offset, (uint32_t)padding, 0}; }
		arena->blocks[at] = (struct fwa_block){start, rounded, (uint32_t)bytes};
		if (tail) { arena->blocks[at + 1] = (struct fwa_block){start + rounded, tail, 0}; }
		memset(arena->memory + start, 0, rounded);
		if (bus) { *bus = aligned_bus; }
		return arena->memory + start;
	}
	return NULL;
}

static uint64_t
fwa_map(const struct fwa_arena *arena, const void *memory, size_t bytes)
{
	if (!arena || !arena->memory || !memory || !bytes) { return 0; }
	uintptr_t pointer = (uintptr_t)memory, base = (uintptr_t)arena->memory;
	if (pointer < base || pointer - base >= arena->bytes || bytes > arena->bytes - (pointer - base)) { return 0; }
	uint32_t offset = (uint32_t)(pointer - base);
	for (uint32_t index = 1; index < arena->count; index++) {
		const struct fwa_block *block = &arena->blocks[index];
		if (block->requested && offset >= block->offset &&
		    offset - block->offset < block->requested &&
		    bytes <= block->requested - (offset - block->offset)) {
			return arena->bus + offset;
		}
	}
	return 0;
}

static int
fwa_free(struct fwa_arena *arena, size_t bytes, void *memory, uint64_t bus)
{
	if (!arena || !arena->memory || !memory || !bytes ||
	    bus < arena->bus || bus - arena->bus >= arena->bytes) { return 1; }
	uint32_t offset = (uint32_t)(bus - arena->bus);
	if ((uintptr_t)memory != (uintptr_t)arena->memory + offset) { return 1; }
	for (uint32_t index = 1; index < arena->count; index++) {
		struct fwa_block *block = &arena->blocks[index];
		if (block->offset != offset || block->requested != bytes) { continue; }
		block->requested = 0;
		if (index + 1 < arena->count && !arena->blocks[index + 1].requested) {
			block->size += arena->blocks[index + 1].size;
			memmove(&arena->blocks[index + 1], &arena->blocks[index + 2],
			    (arena->count - index - 2) * sizeof(*block));
			arena->count--;
		}
		if (index > 1 && !arena->blocks[index - 1].requested) {
			arena->blocks[index - 1].size += block->size;
			memmove(&arena->blocks[index], &arena->blocks[index + 1],
			    (arena->count - index - 1) * sizeof(*block));
			arena->count--;
		}
		return 0;
	}
	return 1;
}

static int
fwa_kfree(struct fwa_arena *arena, void *memory)
{
	if (!arena || !arena->memory || !memory) { return 1; }
	for (uint32_t index = 1; index < arena->count; index++) {
		const struct fwa_block *block = &arena->blocks[index];
		if (block->requested && memory == arena->memory + block->offset) {
			return fwa_free(arena, block->requested, memory, arena->bus + block->offset);
		}
	}
	return 1;
}
#endif
