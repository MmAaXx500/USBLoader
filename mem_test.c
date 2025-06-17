#include <string.h>

#include "mem.h"
#include "mem_internal.h"
#include "unity.h"

#define TEST_MEM_SIZE 256

uint8_t test_mem[TEST_MEM_SIZE] __attribute__((aligned(16)));

static const uint32_t default_align = sizeof(void *);

// Dummy synbols as no linker script is used
uint8_t heap_start[1];
uint8_t heap_end[1];

/**
 * Align ptr to the specified alignment
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
 * Calculate the max number of objects that can be allocated on the heap
 *
 * @param size size of the object
 * @param align object alignment
 *
 * @return number of objects
 */
static uint32_t max_object_count(uint32_t size, uint32_t align) {
	uint8_t *start = test_mem + sizeof(struct block_header);
	uint8_t *addr = align_ptr(start, align);
	uint32_t count = 0;

	while (addr <= &test_mem[TEST_MEM_SIZE]) {
		addr += size;
		if (addr > &test_mem[TEST_MEM_SIZE])
			break;

		count++;

		addr += sizeof(struct block_header);
		addr = align_ptr(addr, align);
	}
	return count;
}

void setUp(void) {
	memset(test_mem, 0, TEST_MEM_SIZE);

	free_block_head = (struct free_block *)test_mem;
	free_block_head->size = TEST_MEM_SIZE;
	free_block_head->next = 0;
}

void tearDown(void) {}

// test if the allocation works
// Edge case: Wrong free block selection
static void test_memalloc_allocate_8_align(void) {
	uint32_t alloc_size = sizeof(uint8_t);
	uint8_t *var = memalloc(alloc_size);

	TEST_ASSERT_NOT_NULL(var);
	TEST_ASSERT_EQUAL_INT(0, (uintptr_t)var % default_align);
}

// test if the allocation works with 128 byte
// Edge case: Wrong free block selection
static void test_memalloc_allocate_128_align(void) {
	uint32_t alloc_size = 128;
	uint8_t *var = memalloc(alloc_size);

	TEST_ASSERT_NOT_NULL(var);
	TEST_ASSERT_EQUAL_INT(0, (uintptr_t)var % default_align);
}

// Edge case: 0 allocation size
static void test_memalloc_allocate_0(void) {
	uint8_t *var = memalloc(0);

	TEST_ASSERT_NULL(var);
}

// Allocation with too large size should fail
// Edge case: Free space size calculation
static void test_memalloc_allocate_too_large(void) {
	uint32_t alloc_size = TEST_MEM_SIZE + 1;
	uint8_t *var = memalloc(alloc_size);

	TEST_ASSERT_NULL(var);
}

// Allocation with too large size should fail
// not possible to allocate TEST_MEM_SIZE because of the allocated block header
// Edge case: Free space size calculation
static void test_memalloc_allocate_too_large2(void) {
	uint32_t alloc_size = TEST_MEM_SIZE;
	uint8_t *var = memalloc(alloc_size);

	TEST_ASSERT_NULL(var);
}

// Allocate the max possible size
// Edge case: Completely filled memory (single object)
static void test_memalloc_allocate_max(void) {
	uint32_t alloc_size = TEST_MEM_SIZE - sizeof(struct block_header);
	uint8_t *var = memalloc(alloc_size);

	TEST_ASSERT_NOT_NULL(var);
	TEST_ASSERT_EQUAL_INT(0, (uintptr_t)var % default_align);
}

// Completely fill the available memory
// Edge case: Completely filled memory (multiple objects)
static void test_memalloc_fill_8(void) {
	char buf[48];
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, default_align);

	for (size_t i = 0; i < max_alloc_nr; i++) {
		uint8_t *var = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(var, buf);
	}

	uint8_t *var = memalloc(alloc_size);
	TEST_ASSERT_NULL(var);
}

// Allocating, freeing, and allocating again should result in the same memory
// location
// Edge case: Wrong object start calculation after free
static void test_memalloc_alloc_reuse_8(void) {
	uint32_t alloc_size = sizeof(uint8_t);
	uint8_t *var = memalloc(alloc_size);

	memfree(var);

	uint8_t *var2 = memalloc(alloc_size);

	TEST_ASSERT_NOT_NULL(var);
	TEST_ASSERT_NOT_NULL(var2);
	TEST_ASSERT_EQUAL_PTR(var, var2);
}

// Allocate all memory and free up one object in the middle.
// Allocating a larger one should fail, same size should succeed.
// Edge case: Wrong block size calculation
static void test_memalloc_fragmented_free_small_8(void) {
	size_t ptrs_len = TEST_MEM_SIZE / 5;
	uint8_t *ptrs[ptrs_len];
	uint32_t alloc_size = sizeof(uint8_t);

	size_t last = 0;
	size_t i = 0;
	do {
		last = i;
		ptrs[i++] = memalloc(alloc_size);
	} while (ptrs[last] != 0);

	if (last > 0)
		last--;

	// at least 3 objects required to have a middle one
	TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(
	    2, last,
	    "Allocation objects are less then required. At least 3 (i=2) must "
	    "be allocated");

	// free some space at the middle
	memfree(ptrs[last / 2]);

	// try to allocate one that should not fit
	uint8_t *var = memalloc(alloc_size + default_align);
	TEST_ASSERT_NULL(var);

	// allocate one that should fit
	var = memalloc(alloc_size);
	TEST_ASSERT_NOT_NULL(var);
}

// Edge case: 0 alignment
static void test_memalloc_aligned_8_align_0(void) {
	uint32_t alloc_size = sizeof(uint8_t);

	uint8_t *var = memalloc_aligned(alloc_size, 0);
	TEST_ASSERT_NULL(var);
}

// Fill the memory with byte aligned objects
// Edge case: align is the smalles possible
static void test_memalloc_aligned_fill_8_align_1(void) {
	char buf[48];
	uint32_t align = 1;
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, align);

	for (size_t i = 0; i < max_alloc_nr; i++) {
		uint8_t *var = memalloc_aligned(alloc_size, align);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(var, buf);
	}

	uint8_t *var = memalloc_aligned(alloc_size, align);
	TEST_ASSERT_NULL(var);
}

// Fill the memory with 64 byte aligned objects
// Edge case: align is larger than the header
static void test_memalloc_aligned_fill_8_align_64(void) {
	char buf[48];
	uint32_t align = 64;
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, align);

	for (size_t i = 0; i < max_alloc_nr; i++) {
		uint8_t *var = memalloc_aligned(alloc_size, align);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(var, buf);
	}

	uint8_t *var = memalloc_aligned(alloc_size, align);
	TEST_ASSERT_NULL(var);
}

// Try to allocate an object with larger alignment than the usable memory
// Edge case: allocating after the memory region
static void test_memalloc_aligned_align_over(void) {
	uint32_t alloc_size = TEST_MEM_SIZE - sizeof(struct block_header);

	uint8_t *var = memalloc_aligned(alloc_size, TEST_MEM_SIZE);
	TEST_ASSERT_NULL(var);
}

// There is no assert. If something is wrong memfree is going to access invalid
// memory and crash the test / cause a warning with sanitizers enabled
// Edge case: free-ing null
static void test_memfree_null(void) { memfree(0); }

// Allocate all memory, free all objects, and allocate all again
// Edge case: free-ing objects with freen blocks before and allocated blocks
// after
static void test_memfree_reuse_full_8(void) {
	char buf[48];
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, default_align);
	uint8_t *ptrs[max_alloc_nr];

	// fill for the first time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	// free all
	for (size_t i = 0; i < max_alloc_nr; i++) {
		memfree(ptrs[i]);
		ptrs[i] = 0;
	}

	// fill for the second time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Second allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	uint8_t *var = memalloc(alloc_size);
	TEST_ASSERT_NULL(var);
}

// Same as previous, but the free-ing is in reverse
// Edge case: free-ing objects with allocated blocks before and free blocks
// after
static void test_memfree_reuse_full_reverse_8(void) {
	char buf[48];
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, default_align);
	uint8_t *ptrs[max_alloc_nr];

	// fill for the first time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	// free all in reverse
	for (size_t i = max_alloc_nr; i != 0; i--) {
		memfree(ptrs[i - 1]);
		ptrs[i - 1] = 0;
	}

	// fill for the second time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Second allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	uint8_t *var = memalloc(alloc_size);
	TEST_ASSERT_NULL(var);
}

// Same as previous, but free-ing first the even ones that the odd ones
// Edge case: free-ing objects with allocated/free blocks on both side
static void test_memfree_reuse_full_alternating_8(void) {
	char buf[48];
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, default_align);
	uint8_t *ptrs[max_alloc_nr];

	// fill for the first time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	// free even indexes
	for (size_t i = 0; i < max_alloc_nr; i += 2) {
		memfree(ptrs[i]);
		ptrs[i] = 0;
	}

	// free odd indexes
	for (size_t i = 1; i < max_alloc_nr; i += 2) {
		memfree(ptrs[i]);
		ptrs[i] = 0;
	}

	// fill for the second time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Second allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	uint8_t *var = memalloc(alloc_size);
	TEST_ASSERT_NULL(var);
}

// Fill the memory with smaller objects, free them, and fill the memory with
// larger objects
// Edge case: free block coalesce not working correctly
static void test_memfree_reuse_larger_8(void) {
	char buf[48];
	uint32_t alloc_size = sizeof(uint8_t);
	uint32_t max_alloc_nr = max_object_count(alloc_size, default_align);
	uint8_t *ptrs[max_alloc_nr];

	// fill for the first time
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	// free all
	for (size_t i = 0; i < max_alloc_nr; i++) {
		memfree(ptrs[i]);
		ptrs[i] = 0;
	}

	// guaranteed to be larger than the previous allocation
	alloc_size = sizeof(uint8_t) + default_align;
	max_alloc_nr = max_object_count(alloc_size, default_align);

	// fill for the second time with larger objects
	for (size_t i = 0; i < max_alloc_nr; i++) {
		ptrs[i] = memalloc(alloc_size);

		snprintf(buf, sizeof(buf), "Second allocation failed: i=%zu", i);
		TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], buf);
	}

	uint8_t *var = memalloc(alloc_size);
	TEST_ASSERT_NULL(var);
}

static void test_memcopy(void) {
#define ARR_SIZE 10
	uint8_t arr1[ARR_SIZE] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	uint8_t arr2[ARR_SIZE] = {0};

	memcopy(arr2, arr1, ARR_SIZE);

	TEST_ASSERT_EQUAL_UINT8_ARRAY(arr1, arr2, ARR_SIZE);
#undef ARR_SIZE
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_memalloc_allocate_8_align);
	RUN_TEST(test_memalloc_allocate_128_align);
	RUN_TEST(test_memalloc_allocate_0);
	RUN_TEST(test_memalloc_allocate_too_large);
	RUN_TEST(test_memalloc_allocate_too_large2);
	RUN_TEST(test_memalloc_allocate_max);
	RUN_TEST(test_memalloc_fill_8);
	RUN_TEST(test_memalloc_alloc_reuse_8);
	RUN_TEST(test_memalloc_fragmented_free_small_8);

	RUN_TEST(test_memalloc_aligned_8_align_0);
	RUN_TEST(test_memalloc_aligned_fill_8_align_1);
	RUN_TEST(test_memalloc_aligned_fill_8_align_64);
	RUN_TEST(test_memalloc_aligned_align_over);

	RUN_TEST(test_memfree_null);
	RUN_TEST(test_memfree_reuse_full_8);
	RUN_TEST(test_memfree_reuse_full_reverse_8);
	RUN_TEST(test_memfree_reuse_full_alternating_8);
	RUN_TEST(test_memfree_reuse_larger_8);

	RUN_TEST(test_memcopy);
	return UNITY_END();
}
