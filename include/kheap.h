#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

/*
 * Kernel Heap Allocator
 * 
 * Simple linked-list allocator for dynamic memory allocation.
 * Uses a first-fit algorithm with block splitting.
 * 
 * Heap structure:
 *   [block_header][data][block_header][data]...
 * 
 * Block header:
 *   - size: size of data area (not including header)
 *   - flags: allocated/free flag
 *   - next: pointer to next block
 */

/* Block flags */
#define KHEAP_FREE      0x00
#define KHEAP_ALLOCATED 0x01
#define KHEAP_MAGIC     0xBEEF

/* Block header */
typedef struct kheap_block {
    struct kheap_block* next;
    uint32_t size;
    uint16_t flags;
    uint16_t magic;
} kheap_block_t;

/* Initialize heap at a virtual address with given size */
void kheap_init(uint32_t heap_start, uint32_t heap_size);

/* Allocate memory (returns virtual address) */
void* kmalloc(size_t size);

/* Allocate aligned memory */
void* kmalloc_aligned(size_t size, uint32_t alignment);

/* Free memory */
void kfree(void* ptr);

/* Reallocate memory */
void* krealloc(void* ptr, size_t new_size);

/* Get heap statistics */
size_t kheap_get_used(void);
size_t kheap_get_free(void);
size_t kheap_get_total(void);

/* Debug: print heap state */
void kheap_dump(void);

#endif
