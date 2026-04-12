/*
 * kheap64.c - Kernel Heap Allocator (64-bit)
 */

#include <stdint.h>
#include <stddef.h>

typedef struct kheap_block {
    struct kheap_block* next;
    uint64_t size;
    uint16_t flags;
    uint16_t magic;
} kheap_block_t;

#define KHEAP_FREE      0x00
#define KHEAP_ALLOCATED 0x01
#define KHEAP_MAGIC     0xBEEF
#define MIN_BLOCK_SIZE  (sizeof(kheap_block_t) + 16)

#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define BLOCK_DATA(b)    ((void*)((uint64_t)(b) + sizeof(kheap_block_t)))
#define DATA_BLOCK(d)    ((kheap_block_t*)((uint64_t)(d) - sizeof(kheap_block_t)))

static kheap_block_t* heap_first = 0;
static uint64_t bytes_used = 0;
static uint64_t bytes_free = 0;

extern void* pmm_alloc_page(void);
extern void paging_map(uint64_t vaddr, uint64_t paddr, uint64_t flags);
#define PAGE_PRESENT  0x01
#define PAGE_WRITABLE 0x02

void kheap_init(uint64_t start, uint64_t size) {
    /* Allocate and map heap pages */
    for (uint64_t addr = start; addr < start + size; addr += 4096) {
        void* phys = pmm_alloc_page();
        if (phys) {
            paging_map(addr, (uint64_t)phys, PAGE_WRITABLE);
        }
    }
    
    heap_first = (kheap_block_t*)start;
    heap_first->next = 0;
    heap_first->size = size - sizeof(kheap_block_t);
    heap_first->flags = KHEAP_FREE;
    heap_first->magic = KHEAP_MAGIC;
    
    bytes_used = 0;
    bytes_free = size - sizeof(kheap_block_t);
}

static void split_block(kheap_block_t* block, uint64_t size) {
    uint64_t remaining = block->size - size - sizeof(kheap_block_t);
    if (remaining < MIN_BLOCK_SIZE) return;
    
    kheap_block_t* new_block = (kheap_block_t*)((uint64_t)block + sizeof(kheap_block_t) + size);
    new_block->next = block->next;
    new_block->size = remaining;
    new_block->flags = KHEAP_FREE;
    new_block->magic = KHEAP_MAGIC;
    
    block->size = size;
    block->next = new_block;
    
    bytes_free -= sizeof(kheap_block_t);
}

static void merge_blocks(void) {
    kheap_block_t* block = heap_first;
    while (block && block->next) {
        if (!(block->flags & KHEAP_ALLOCATED) && !(block->next->flags & KHEAP_ALLOCATED)) {
            uint64_t expected = (uint64_t)block + sizeof(kheap_block_t) + block->size;
            if ((uint64_t)block->next == expected) {
                kheap_block_t* next = block->next;
                block->size += sizeof(kheap_block_t) + next->size;
                block->next = next->next;
                bytes_free += sizeof(kheap_block_t);
                continue;
            }
        }
        block = block->next;
    }
}

void* kmalloc(uint64_t size) {
    if (size == 0) return 0;
    size = ALIGN_UP(size, 16);
    
    kheap_block_t* block = heap_first;
    while (block) {
        if (block->magic != KHEAP_MAGIC) return 0;
        if (!(block->flags & KHEAP_ALLOCATED) && block->size >= size) {
            split_block(block, size);
            block->flags = KHEAP_ALLOCATED;
            bytes_used += block->size;
            bytes_free -= block->size;
            return BLOCK_DATA(block);
        }
        block = block->next;
    }
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    kheap_block_t* block = DATA_BLOCK(ptr);
    if (block->magic != KHEAP_MAGIC) return;
    if (!(block->flags & KHEAP_ALLOCATED)) return;
    
    block->flags = KHEAP_FREE;
    bytes_used -= block->size;
    bytes_free += block->size;
    
    merge_blocks();
}

void* krealloc(void* ptr, uint64_t new_size) {
    if (new_size == 0) { kfree(ptr); return 0; }
    if (!ptr) return kmalloc(new_size);
    
    kheap_block_t* block = DATA_BLOCK(ptr);
    if (block->magic != KHEAP_MAGIC) return 0;
    if (new_size <= block->size) return ptr;
    
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;
    
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    for (uint64_t i = 0; i < block->size && i < new_size; i++) {
        dst[i] = src[i];
    }
    
    kfree(ptr);
    return new_ptr;
}

uint64_t kheap_get_used(void) { return bytes_used; }
uint64_t kheap_get_free(void) { return bytes_free; }
