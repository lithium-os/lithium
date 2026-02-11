#include <stdint.h>
#include <stddef.h>
#include "../include/serial.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/limine_requests.h"

// SLAB metadata stored at the start of each page
struct slab {
    struct kmem_cache *cache;   // Which cache owns this
    void *freelist;             // Head of free object list
    int free_count;             // Number of free objects
    int total_count;            // Total objects in slab
    struct slab *next;
};

// Cache for specific obj size
struct kmem_cache {
    const char *name;
    size_t object_sz;           // Size of each object
    size_t objects_per_slab;    // How many objects fit in a slab
    struct slab *partial;       // Slabs with some free objects
    struct slab *full;          // Slabs with no free objects
};

#define NUM_CACHES 32
static struct kmem_cache caches[NUM_CACHES];

// Cache sizes in bytes
static const size_t cache_sizes[NUM_CACHES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 3072, 4096
};

// Tossing the heap in virtual mem after the kernel
#define HEAP_START 0xFFFFFFFF90000000ULL
static uint64_t heap_current = HEAP_START;

// Helper: Alloc virtual address range for heap
static void *heap_alloc_pages(size_t num_pages) {}

// Helper: Create a new slab for a cache
static struct slab *slab_create(struct kmem_cache *cache) {}

// Helper: Init a cache
static void kmem_cache_init(struct kmem_cache *cache, const char *name, size_t object_size) {}

// Allocate an obj from a cache
void *kmem_cache_alloc(struct kmem_cache *cache);

// Free an object back to its cache
void kmem_cache_free(struct kmem_cache *cache, void *ptr) {}

// Initialize the kernel allocator
void kalloc_init(void) {
    serial_puts("Initializing kernel allocator (SLAB)...\n");
    
    for (int i = 0; i < NUM_CACHES; i++) {
        char name[32];
        // Simple name generation (I should come back to this)
        kmem_cache_init(&caches[i], "kmalloc-cache", cache_sizes[i]);
        
        serial_puts("  Cache ");
        serial_put_dec(cache_sizes[i]);
        serial_puts(" bytes: ");
        serial_put_dec(caches[i].objects_per_slab);
        serial_puts(" objects per slab\n");
    }
    
    serial_puts("Kernel allocator ready!\n");
}

// General purpose allocator: picks an appropriate cache
void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    // Find smallest cache that fits
    for (int i = 0; i < NUM_CACHES; i++) {
        if (size <= cache_sizes[i]) {
            return kmem_cache_alloc(&caches[i]);
        }
    }
    
    // When size > 4KiB, directly allocate
    serial_puts("KALLOC: Large allocation (");
    serial_put_dec(size);
    serial_puts(" bytes) not yet supported!\n");
    return NULL;
}

// Free memory allocated with kmalloc
void kfree(void *ptr) {
    if (!ptr) return;
    
    // Round down to page boundary to find slab, free back to cache
    uint64_t slab_addr = (uint64_t)ptr & ~0xFFFULL;
    struct slab *slab = (struct slab *)slab_addr;
    kmem_cache_free(slab->cache, ptr);
}