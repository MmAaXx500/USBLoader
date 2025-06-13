#pragma once

#include <stdint.h>

/**
 * Initialize the memory allocator. Must be called before any calls to memalloc,
 * memalloc_aligned, or memfree.
 */
void init_memory(void);

/**
 * Allocate contiguous memory of the specified size and return a pointer to it.
 * The default alignment size is the size of a pointer.
 *
 * @param size Size of the contiguous memory to be allocated
 * @return Pointer to the allocated memory or NULL if the allocation failed
 */
void *memalloc(uint32_t size);

/**
 * Allocate contiguous memory of the specified size and alignment, and return a
 * pointer to it.
 *
 * @param size Size of the contiguous memory to be allocated
 * @param align alignment of the returned memory in bytes
 * @return Pointer to the allocated memory or NULL if the allocation failed
 */
void *memalloc_aligned(uint32_t size, uint32_t align);

/**
 * Free allocated memory.
 *
 * @param ptr Pointer that points to the memory that should be free-ed. It must
 * be the same pointer that is returned by memalloc or memalloc_aligned
 */
void memfree(void *ptr);

/**
 * Copy `size` bytes from `src` to `dst`.
 *
 * @param dst pointer to the destination memory
 * @param src pointer to the source memory
 * @param size size in bytes to be copied
 * @return Same as dst parameter
 */
void *memcopy(void *dst, void *src, uint32_t size);
