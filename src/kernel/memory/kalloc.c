#include <stdint.h>
#include <stddef.h>
#include "../include/serial.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/limine_requests.h"

// Forward declarations (used before definitions in this file)
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);

static void *kmalloc_large(size_t size);
static void  kfree_large(void *ptr);
static int   is_large_alloc(void *ptr);

// SLAB metadata stored at the start of each page
struct slab {
    struct kmem_cache *cache;   // Which cache owns this
    void *freelist;             // Head of free object list
    int free_count;             // Number of free objects
    int total_count;            // Total objects in slab
    struct slab *next;
    uint64_t phys_addr;         // Physical address of slab page
};

// Cache for specific obj size
struct kmem_cache {
    const char *name;
    size_t object_sz;           // Size of each object
    size_t objects_per_slab;    // How many objects fit in a slab
    struct slab *partial;       // Slabs with some free objects
    struct slab *full;          // Slabs with no free objects
};

// Large allocation header (for >4KB allocations)
#define LARGE_ALLOC_MAGIC 0x4C41524745414C4CULL /* "LARGEALL" */

// Large allocation header (for >4KB allocations)
struct large_alloc {
    uint64_t magic;             // Sanity check
    uint64_t vaddr;             // Base virtual address returned to caller
    size_t size;                // Total size in bytes
    size_t num_pages;           // Number of pages allocated
    struct large_alloc *next;
    uint64_t phys_addrs[];      // Flexible array of physical addrs
};


#define NUM_CACHES 10
static struct kmem_cache caches[NUM_CACHES];

// Cache sizes in bytes
static const size_t cache_sizes[NUM_CACHES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 3072, 4096
};

// List of large allocations
static struct large_alloc *large_allocs = NULL;

static struct large_alloc *large_find(uint64_t vaddr, struct large_alloc ***link_out) {
    struct large_alloc **link = &large_allocs;

    while (*link) {
        if ((*link)->vaddr == vaddr) {
            if (link_out) {
                *link_out = link;
            }
            return *link;
        }
        link = &(*link)->next;
    }

    return NULL;
}


// Tossing the heap in virtual mem after the kernel
#define HEAP_START 0xFFFFFFFF90000000ULL
static uint64_t heap_current = HEAP_START;

// Forward declare functions
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);

// Helper: Alloc virtual address range for heap
static void *heap_alloc_pages(size_t num_pages, uint64_t *out_phys) {
    uint64_t v_addr = heap_current;
    heap_current += num_pages * 4096;
    
    uint64_t first_phys = 0;
    
    for (size_t i = 0; i < num_pages; i++) {
        void *phys_virt = pmm_alloc();
        if (!phys_virt) {
            serial_puts("KALLOC: Out of physical memory!\n");
            return NULL;
        }

        uint64_t phys = (uint64_t)phys_virt - hhdm_request.response->offset;
        
        if (i == 0) {
            first_phys = phys;
        }
        
        if (vmm_map(v_addr + (i * 4096), phys, VMM_WRITE) != 0) {
            serial_puts("KALLOC: Failed to map heap page!\n");
            return NULL;
        }
    }
    
    if (out_phys) {
        *out_phys = first_phys;
    }

    return (void *)v_addr;
}

// Helper: Create a new slab for a cache
static struct slab *slab_create(struct kmem_cache *cache) {
    uint64_t phys_addr;
    void *slab_mem = heap_alloc_pages(1, &phys_addr);
    if (!slab_mem)
        return NULL;

    struct slab *slab = (struct slab *)slab_mem;
    slab->cache = cache;
    slab->free_count = cache->objects_per_slab;
    slab->total_count = cache->objects_per_slab;
    slab->next = NULL;
    slab->phys_addr = phys_addr;

    // Objects after slab header 
    size_t header_size = sizeof(struct slab);
    size_t aligned_header = (header_size + 15) & ~15;
    void *objects_start = (uint8_t *)slab_mem + aligned_header;

    // Building the freelist
    slab->freelist = objects_start;
    uint8_t *obj = (uint8_t *)objects_start;
    
    for (size_t i = 0; i < cache->objects_per_slab - 1; i++) {
        void **next_ptr = (void **)obj;
        *next_ptr = obj + cache->object_sz;
        obj += cache->object_sz;
    }

    // The last object points to nil
    void **last = (void **)obj;
    *last = NULL;
    
    return slab;
}

// Helper: Destroy the slab and return memory to VMM/PMM
static void slab_destroy(struct kmem_cache *cache, struct slab *slab) {
    // Remove from partial list
    if (cache->partial == slab) {
        cache->partial = slab->next;
    } else {
        struct slab *prev = cache->partial;
        while (prev && prev->next != slab) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = slab->next;
        }
    }

    uint64_t v_addr = (uint64_t)slab;
    uint64_t phys = slab->phys_addr;
    
    // Unmap the virtual page
    vmm_unmap(v_addr);
    
    // Free the physical page back to PMM
    void *phys_virt = (void *)(phys + hhdm_request.response->offset);
    pmm_free(phys_virt);
} 

// Helper: Init a cache
static void kmem_cache_init(struct kmem_cache *cache, const char *name, size_t object_size) {
    cache->name = name;
    cache->object_sz = object_size;
    cache->partial = NULL;
    cache->full = NULL;

    size_t usable_size = 4096 - sizeof(struct slab) - 16;
    cache->objects_per_slab = usable_size / object_size;
    
    if (cache->objects_per_slab == 0) {
        cache->objects_per_slab = 1;
    }
}

// Allocate an object from a cache
void *kmem_cache_alloc(struct kmem_cache *cache) {
    struct slab *slab = cache->partial;
    
    if (!slab) {
        // No partial slabs, create a new one
        slab = slab_create(cache);
        if (!slab) {
            return NULL;
        }
        slab->next = cache->partial;
        cache->partial = slab;
    }
    
    // Pop it from freelist
    void *obj = slab->freelist;
    if (!obj) {
        serial_puts("KALLOC: Slab freelist empty but free_count > 0!\n");
        return NULL;
    }
    
    // Update freelist to next free object
    void **next_ptr = (void **)obj;
    slab->freelist = *next_ptr;
    slab->free_count--;
    
    // If slab is now full, move it to full list
    if (slab->free_count == 0) {
        cache->partial = slab->next;
        slab->next = cache->full;
        cache->full = slab;
    }
    
    return obj;
}

// Free an object back to its cache
void kmem_cache_free(struct kmem_cache *cache, void *ptr) {
    if (!ptr) return;
    
    // Find which slab this object belongs to by rounding down
    uint64_t slab_addr = (uint64_t)ptr & ~0xFFFULL;
    struct slab *slab = (struct slab *)slab_addr;
    
    // Sanity check
    if (slab->cache != cache) {
        serial_puts("KALLOC: Object freed to wrong cache!\n");
        return;
    }

    int was_full = (slab->free_count == 0);
    
    // Push object back onto freelist
    void **next_ptr = (void **)ptr;
    *next_ptr = slab->freelist;
    slab->freelist = ptr;
    slab->free_count++;
    
    // If slab was full, move it back to partial list
    if (was_full) {
        if (cache->full == slab) {
            cache->full = slab->next;
        } else {
            struct slab *prev = cache->full;
            while (prev && prev->next != slab) {
                prev = prev->next;
            }
            if (prev) {
                prev->next = slab->next;
            }
        }

        slab->next = cache->partial;
        cache->partial = slab;
    }

    // If the slab is now completely empty, consider freeing it
    if (slab->free_count == slab->total_count) {
        int empty_count = 0;
        struct slab *s = cache->partial;

        while (s) {
            if (s->free_count == s->total_count) {
                empty_count++;
            }

            s = s->next;
        }

        // Keep at least one empty slab as reserve, free the rest
        if (empty_count > 1) {
            slab_destroy(cache, slab);
        }
    }
}

// Allocate large (>4KB) memory directly via pages
static void *kmalloc_large(size_t size) {
    // Calculate number of pages needed
    size_t num_pages = (size + 4095) / 4096;
    
    // Allocate header + space for physical address array using a small cache
    size_t header_size = sizeof(struct large_alloc) + (num_pages * sizeof(uint64_t));
    if (header_size > 4096) {
        serial_puts("KALLOC: large alloc header too big (>4KiB); allocation refused");
        return NULL;
    }
    struct large_alloc *alloc = kmalloc(header_size);
    if (!alloc) {
        return NULL;
    }
    
    // Allocate the pages
    uint64_t v_addr = heap_current;
    heap_current += num_pages * 4096;
    
    for (size_t i = 0; i < num_pages; i++) {
        void *phys_virt = pmm_alloc();
        if (!phys_virt) {
            // Cleanup on failure
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(v_addr + (j * 4096));
                void *pv = (void *)(alloc->phys_addrs[j] + hhdm_request.response->offset);
                pmm_free(pv);
            }
            kfree(alloc);
            return NULL;
        }
        
        uint64_t phys = (uint64_t)phys_virt - hhdm_request.response->offset;
        alloc->phys_addrs[i] = phys;
        
        if (vmm_map(v_addr + (i * 4096), phys, VMM_WRITE) != 0) {
            // Cleanup on failure
            pmm_free(phys_virt);
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(v_addr + (j * 4096));
                void *pv = (void *)(alloc->phys_addrs[j] + hhdm_request.response->offset);
                pmm_free(pv);
            }
            kfree(alloc);
            return NULL;
        }
    }
    
    alloc->magic = LARGE_ALLOC_MAGIC;
    alloc->vaddr = v_addr;
    alloc->size = size;
    alloc->num_pages = num_pages;
    alloc->next = large_allocs;
    large_allocs = alloc;
    
    return (void *)v_addr;
}

// Free large allocation
static void kfree_large(void *ptr) {
    if (!ptr) {
        return;
    }

    uint64_t vaddr = (uint64_t)ptr;

    struct large_alloc **link = NULL;
    struct large_alloc *alloc = large_find(vaddr, &link);

    if (!alloc) {
        serial_puts("kfree_large: pointer not found in large_allocs\\n");
        return;
    }

    if (alloc->magic != LARGE_ALLOC_MAGIC) {
        serial_puts("kfree_large: bad magic (corrupt header?)\\n");
        return;
    }

    for (size_t i = 0; i < alloc->num_pages; i++) {
        uint64_t page_vaddr = alloc->vaddr + (i * 4096ULL);
        vmm_unmap(page_vaddr);

        void *pv = (void *)(alloc->phys_addrs[i] + hhdm_request.response->offset);
        pmm_free(pv);
    }

    // Unlink from list
    if (link) {
        *link = alloc->next;
    }

    // Free the header node (allocated via kmalloc)
    kfree(alloc);
}

// Check if pointer is a large allocation
static int is_large_alloc(void *ptr) {
    if (!ptr) {
        return 0;
    }
    return large_find((uint64_t)ptr, NULL) != NULL;
}

// Initialize the kernel allocator
void kalloc_init(void) {
    serial_puts("Initializing kernel allocator (SLAB)...\n");
    
    for (int i = 0; i < NUM_CACHES; i++) {
        kmem_cache_init(&caches[i], "kmalloc-cache", cache_sizes[i]);
        
        serial_puts("  Cache ");
        serial_put_dec(cache_sizes[i]);
        serial_puts(" bytes: ");
        serial_put_dec(caches[i].objects_per_slab);
        serial_puts(" objects per slab\n");
    }
    
    serial_puts("Kernel allocator ready!\n");
}

// General purpose allocator
void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    // Large allocation?
    if (size > 4096) {
        return kmalloc_large(size);
    }
    
    // Find smallest cache that fits
    for (int i = 0; i < NUM_CACHES; i++) {
        if (size <= cache_sizes[i]) {
            return kmem_cache_alloc(&caches[i]);
        }
    }
    
    return NULL;
}

// Reallocate memory
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    // Determine old size
    size_t old_size = 0;
    
    if (is_large_alloc(ptr)) {
        // Find in large alloc list
        struct large_alloc *alloc = large_allocs;
        while (alloc) {
            // Need better tracking here
            alloc = alloc->next;
        }
        // For now, just allocate new and copy
        old_size = new_size; // Assume worst case
    } else {
        // Slab allocation - find which cache
        uint64_t slab_addr = (uint64_t)ptr & ~0xFFFULL;
        struct slab *slab = (struct slab *)slab_addr;
        old_size = slab->cache->object_sz;
    }
    
    // If new size fits in same allocation, just return it
    if (new_size <= old_size) {
        return ptr;
    }
    
    // Allocate new block
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy old data
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < old_size; i++) {
        dst[i] = src[i];
    }
    
    // Free old block
    kfree(ptr);
    
    return new_ptr;
}

// Free memory
void kfree(void *ptr) {
    if (!ptr) return;
    
    if (is_large_alloc(ptr)) {
        kfree_large(ptr);
        return;
    }
    
    // Round down to page boundary to find slab
    uint64_t slab_addr = (uint64_t)ptr & ~0xFFFULL;
    struct slab *slab = (struct slab *)slab_addr;
    kmem_cache_free(slab->cache, ptr);

    serial_puts("Freed memory.\n");
}