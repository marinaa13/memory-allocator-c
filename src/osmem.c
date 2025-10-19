// SPDX-License-Identifier: BSD-3-Clause

#include "../utils/osmem.h"
#include "../utils/block_meta.h"

#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BLOCK_META_SIZE sizeof(struct block_meta)
#define MMAP_THRESHOLD (128 * 1024)
#define PAGE_SIZE getpagesize()
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define FAIL_CODE ((void *)-1)

struct block_meta *list;

int first_alloc;

size_t current_threshold = MMAP_THRESHOLD;

void coalesce_blocks(void)
{
	struct block_meta *current = list;
	struct block_meta *next;

	while (current && current->next) {
		next = current->next;

		if (current->status == STATUS_FREE && next->status == STATUS_FREE) {
			current->size += next->size + BLOCK_META_SIZE;
			current->next = next->next;

			if (next->next)
				next->next->prev = current;
		} else {
			current = current->next;
		}
	}
}

void add_mapped_elem(struct block_meta *new_block)
{
	// if there is no block in the list => this is the first block
	if (!first_alloc) {
		list = new_block;
		new_block->next = NULL;
		new_block->prev = NULL;
		return;
	}

	// if there is a block in the list => add the new block at the end
	struct block_meta *current = list;
	struct block_meta *last = NULL;

	while (current) {
		last = current;
		current = current->next;
	}

	last->next = new_block;
	new_block->prev = last;
	new_block->next = NULL;
}

struct block_meta *find_best_free_block(size_t size)
{
	size_t min_size = INT_MAX;
	struct block_meta *current = list;
	struct block_meta *best_block = NULL;

	coalesce_blocks();

	current = list;
	while (current) {
		if (current->status == STATUS_FREE)
			if (current->size >= size && current->size < min_size) {
				min_size = current->size;
				best_block = current;
		}
		current = current->next;
	}

	// try and split the block
	size_t total_size = size + BLOCK_META_SIZE;

	if (best_block != NULL && best_block->size > total_size) {
		struct block_meta *new_block = (struct block_meta *)((void *)best_block + total_size);

		new_block->status = STATUS_FREE;
		new_block->size = best_block->size - total_size;
		new_block->prev = best_block;
		new_block->next = best_block->next;
		if (best_block->next)
			best_block->next->prev = new_block;

		best_block->next = new_block;
		best_block->size = size;
	}

	if (best_block != NULL) {
		best_block->status = STATUS_ALLOC;
		return best_block;
	}

	// If best_block == null => didn't find a block, need to try and extend the heap
	struct block_meta *last = list;

	while (last->next)
		last = last->next;

	if (last->status == STATUS_FREE) {
		void *result = sbrk(size - last->size);

		if (result == FAIL_CODE)
			DIE(1, "sbrk failed");

		last->size += size - last->size;
		last->status = STATUS_ALLOC;

		return last;
	}

	// If we didn't find a block and we didn't extend the heap, we need to allocate a new block
	struct block_meta *new_block = (struct block_meta *)sbrk(size + BLOCK_META_SIZE);

	if (new_block == FAIL_CODE)
		DIE(1, "sbrk failed");

	new_block->size = size;
	new_block->prev = last;
	new_block->next = NULL;
	new_block->status = STATUS_ALLOC;
	last->next = new_block;

	return new_block;
}

void *os_malloc(size_t size)
{
	if (size <= 0)
		return NULL;

	size = ALIGN(size);
	size_t total_size = size + ALIGN(BLOCK_META_SIZE);

	// if the size is bigger than the threshold, we need to use mmap instead of sbrk
	if (total_size > current_threshold) {
		struct block_meta *new_block = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (new_block == FAIL_CODE)
			DIE(1, "mmap failed");

		new_block->size = size;
		new_block->status = STATUS_MAPPED;

		add_mapped_elem(new_block);
		return (void *)(new_block + 1);
	}

	// because we didn t pass the threshold, use sbrk
	if (first_alloc == 0) {
		first_alloc = 1;

		struct block_meta *first_block = (struct block_meta *)sbrk(MMAP_THRESHOLD);

		if (first_block == FAIL_CODE)
			DIE(1, "sbrk failed");

		first_block->status = STATUS_FREE;
		first_block->size = MMAP_THRESHOLD - BLOCK_META_SIZE;
		first_block->next = NULL;
		first_block->prev = NULL;

		list = first_block;
	}
	struct block_meta *new_block = find_best_free_block(size);

	return (void *)(new_block + 1);
}

void os_free(void *ptr)
{
	if (!ptr)
		return;

	struct block_meta *block = (struct block_meta *)ptr - 1;

	if (block->status == STATUS_FREE || !block)
		return;

	if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
	} else {
		int return_val = munmap(block, block->size + BLOCK_META_SIZE);

		list = NULL;

		if (return_val == -1)
			DIE(1, "munmap failed");
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0)
		return NULL;

	// use malloc, but change the threshold to PAGE_SIZE
	current_threshold = PAGE_SIZE;
	void *alloc_zone = os_malloc(nmemb * size);

	memset(alloc_zone, 0, nmemb * size);

	current_threshold = MMAP_THRESHOLD;
	return alloc_zone;
}

void *os_realloc(void *ptr, size_t size)
{
	if (ptr == NULL)
		return os_malloc(size);

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *old_block = (struct block_meta *)ptr - 1;

	if (old_block->status == STATUS_FREE || old_block == NULL)
		return NULL;

	size_t new_total_size = ALIGN(size) + ALIGN(BLOCK_META_SIZE);
	size_t old_size = old_block->size;

	if (old_block->status == STATUS_MAPPED || new_total_size >= MMAP_THRESHOLD) {
		void *new_ptr = os_malloc(size);

		if (old_size < size)
			memcpy(new_ptr, ptr, old_size);
		else
			memcpy(new_ptr, ptr, size);

		os_free(ptr);
		return new_ptr;
	}

	if (old_size == ALIGN(size)) {
		return ptr;
	} else if (old_size > ALIGN(size)) {
		if (old_size > new_total_size) {
			struct block_meta *new_block = (struct block_meta *)((void *)old_block + new_total_size);

			new_block->status = STATUS_FREE;
			new_block->size = old_block->size - new_total_size;
			new_block->prev = old_block;
			new_block->next = old_block->next;
			if (old_block->next)
				old_block->next->prev = new_block;

			old_block->next = new_block;
			old_block->size = ALIGN(size);
			old_block->status = STATUS_ALLOC;

			return (void *)(old_block + 1);
		}
	}
	void *new_ptr = os_malloc(size);

	if (!new_ptr)
		return NULL;

	if (old_size < size)
		memcpy(new_ptr, ptr, old_size);
	else
		memcpy(new_ptr, ptr, size);

	os_free(ptr);

	return new_ptr;
}
