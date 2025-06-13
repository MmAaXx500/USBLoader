#include <stdbool.h>
#include <stdint.h>

#include "mem.h"
#include "mem_internal.h"

/*
 * This file implements a first fit memory allocator. Supports alignment of the
 * allocated memory and coalesce of free blocks to reduce fragmentation.
 *
 * Key components:
 * - `init_memory()` initializes the memory allocator
 * - `memalloc()` and `memalloc_aligned()` allocate memory with optional
 * alignment
 * - `memfree()` deallocates and attempts to coalesce adjacent free blocks
 * - Other memory related functions: `memcopy()`
 */

extern uint8_t *heap_start;
extern uint8_t *heap_end;

struct free_block *free_block_head = 0;

void init_memory(void) {
	free_block_head = (struct free_block *)heap_start;
	free_block_head->size = (uint32_t)(heap_end - heap_start);
	free_block_head->next = 0;
}

/**
 * Align `ptr` to the specified alignment
 *
 * @param ptr pointer to be aligned
 * @param align alignemnt
 *
 * @return ptr aligned
 */
static void *align_ptr(const void *ptr, uint32_t align) {
	return (void *)(((uintptr_t)ptr + (align - 1)) & ~((uintptr_t)align - 1));
}

/**
 * Calculate a suitably aligned address inside a free memory block. Takes into
 * account the control structures to be placed within the block
 *
 * @param block pointer to a free block
 * @param align alignemnt
 *
 * @return pointer to the aligned memory address
 */
static uint8_t *aligned_mem_start(const struct free_block *block,
                                  uint32_t align) {
	return align_ptr((uint8_t *)block + sizeof(struct block_header), align);
}

/**
 * Calcualte the usable memory size inside a free block that is aligned to the
 * specified alignment.
 *
 * @param block poitner to a free block
 * @param align alignment
 * @return Size that remains in the block after the alignment
 */
static uint32_t aligned_size(const struct free_block *block, uint32_t align) {
	uintptr_t org_start = (uintptr_t)block;
	uintptr_t mem_start = (uintptr_t)aligned_mem_start(block, align);

	if (mem_start - org_start > block->size)
		return 0;

	return (uint32_t)(block->size - (mem_start - org_start));
}

/**
 * Determine if two fee blocks can be merged
 *
 * @param a free block that is before block b
 * @param b free block that is after block a
 * @return bool
 */
static bool can_merge(const struct free_block *a, const struct free_block *b) {
	uint8_t *a_end = (uint8_t *)a + a->size;
	return a_end == (uint8_t *)b;
}

/**
 * Coalesce free blocks that are adjacent to each other. Block that are merged
 * into an other one are set to a NULL pointer as they are not longer exists.
 * Memory adresses of the blocks should be `prev` < `middle` < `next`
 *
 * @param prev 1st free memory block
 * @param middle 2nd free memory block
 * @param next 3rd free memory block
 */
static void coalesce_blocks(struct free_block **prev,
                            struct free_block **middle,
                            struct free_block **next) {
	bool merge_prev = false;
	if (*prev != 0) {
		merge_prev = can_merge(*prev, *middle);
	}

	bool merge_next = false;
	if (*next != 0) {
		merge_next = can_merge(*middle, *next);
	}

	if (merge_prev && merge_next) {
		// link the prev to the next block and update size
		// they are all adjacent
		(*prev)->next = (*next)->next; // next can't be null as it is mergeable
		(*prev)->size += (*middle)->size + (*next)->size;

		*middle = 0;
		*next = 0;
		return;
	}

	if (merge_prev) {
		(*prev)->size += (*middle)->size;
		(*prev)->next = *next;

		*middle = 0;
		return;
	}

	if (merge_next) {
		// the next is ereased, become part of the middle block
		(*middle)->next = (*next)->next;
		(*middle)->size += (*next)->size;

		*next = 0;
		return;
	}
}

void *memalloc(uint32_t size) { return memalloc_aligned(size, sizeof(void *)); }
void *memalloc_aligned(uint32_t size, uint32_t align) {
	if (size == 0 || align == 0)
		return 0;

	struct free_block *prev = 0;
	struct free_block *curr = free_block_head;

	while (curr != 0 && aligned_size(curr, align) < size) {
		prev = curr;
		curr = curr->next;
	}

	if (curr == 0)
		return 0;

	uint8_t *new_mem = aligned_mem_start(curr, align);

	if (new_mem == 0)
		return 0;

	// Do not update the new_header->size here, may overwrite curr.
	// Using any field of new_header is undefined until the end of the function
	struct block_header *new_header = (struct block_header *)new_mem - 1;
	uint32_t new_header_size = size + sizeof(struct block_header);

	/*
	 * The state now:
	 * ----------------------------------------------------
	 * |...| free |...|        free         |...|free|... |
	 * ----------------------------------------------------
	 *     ^prev      ^curr                     ^curr->next
	 *                |prev->next
	 *                |new_header
	 */

	// Create a new free block after the allocated block

	uint32_t free_after =
	    curr->size - (uint32_t)((new_mem + size) - (uint8_t *)curr);
	if (free_after >= sizeof(struct free_block)) {
		// there is enough space remaining to allocate a new block
		struct free_block l_curr = *curr;
		struct free_block *new_free_blk =
		    (struct free_block *)((uint8_t *)new_header + new_header_size);
		new_free_blk->size = free_after;
		new_free_blk->next = l_curr.next;

		if (prev != 0)
			prev->next = new_free_blk;
		else {
			free_block_head = new_free_blk;
		}
	} else {
		// otherwise the space is lost
		// when it is free'd it will be available again

		new_header_size += free_after;

		if (prev != 0)
			prev->next = curr->next;
		else {
			// one after curr is the first block
			free_block_head = curr->next;
		}
	}

	/*
	 * The state now:
	 *                |   previously free   |
	 * ----------------------------------------------------
	 * |...| free |...| f |  used  |  free  |...|free|... |
	 * ----------------------------------------------------
	 *     ^prev      ^   ^        ^            ^curr->next
	 *                |   |        |new_free_blk
	 *                |   |        |prev->next
	 *                |   |new_header
	 *                |curr
	 *
	 * Note: curr might be == new_header if no dead space created from the
	 * alignment
	 *
	 * Note: new_free_blk is not created and prev->next == curr->next if the
	 * free space was exactly the same that was required
	 */

	// Create a new free block before the allocated block if enough space is
	// created from the alignment

	uint32_t free_before = (uint32_t)(new_mem - (uint8_t *)curr)
	                       - (uint32_t)sizeof(struct block_header);
	if (free_before >= sizeof(struct free_block)) {
		// there is enough space before the allocated space to allocate a new
		// free block
		curr->size = free_before;
		if (prev != 0) {
			curr->next = prev->next;
			prev->next = curr;
		} else {
			curr->next = free_block_head;
			free_block_head = curr;
		}

		// impossible to merge curr with curr->next
		coalesce_blocks(&prev, &curr, &curr->next);

		new_header->start_off = 0;
	} else {
		// no space for a new free block
		// to avoid loosing space indicate in the block header that the "real"
		// start is before the block header
		// free_before might be 0 here
		new_header->start_off = free_before;
	}

	new_header->size = new_header_size;

	return new_mem;
}

void memfree(void *ptr) {
	if (ptr == 0)
		return;

	struct free_block *prev = 0;
	struct free_block *curr = free_block_head;

	while (curr != 0 && (void *)curr < ptr) {
		prev = curr;
		curr = curr->next;
	}

	uint8_t *block_start = (uint8_t *)((struct block_header *)ptr - 1);

	// block_header is local
	struct block_header block_header = *(struct block_header *)block_start;

	// there is a chance of overwriting real block header
	struct free_block *new_free_blk =
	    (struct free_block *)(block_start - block_header.start_off);
	new_free_blk->next = curr;
	new_free_blk->size = block_header.size + block_header.start_off;

	if (prev == 0) {
		free_block_head = new_free_blk;
	} else
		prev->next = new_free_blk;

	coalesce_blocks(&prev, &new_free_blk, &curr);
}

void *memcopy(void *dst, void *src, uint32_t size) {
	uint8_t *src8 = src;
	uint8_t *dst8 = dst;
	uint8_t *end8 = dst8 + size;

	while (dst8 < end8)
		*dst8++ = *src8++;

	return dst;
}
