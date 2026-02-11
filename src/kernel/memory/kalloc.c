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

#define NUM_CACHES 10
static struct kmem_cache caches[NUM_CACHES];

// Cache sizes in bytes
static const size_t cache_sizes[NUM_CACHES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 3072, 4096
};

// Tossing the heap in virtual mem after the kernel
#define HEAP_START 0xFFFFFFFF90000000ULL
static uint64_t heap_current = HEAP_START;

// Helper: Alloc virtual address range for heap
// Now also returns the physical address of the first page
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
    serial_puts("slab_create: allocating page...\n");
    
    uint64_t phys_addr;
    void *slab_mem = heap_alloc_pages(1, &phys_addr);
    if (!slab_mem) {
        serial_puts("slab_create: heap_alloc_pages failed!\n");
        return NULL;
    }

    serial_puts("slab_create: got mem at ");
    serial_put_hex((uint64_t)slab_mem);
    serial_puts(", phys ");
    serial_put_hex(phys_addr);
    serial_puts("\n");
    
    serial_puts("slab_create: initializing header...\n");
    struct slab *slab = (struct slab *)slab_mem;
    slab->cache = cache;
    slab->free_count = cache->objects_per_slab;
    slab->total_count = cache->objects_per_slab;
    slab->next = NULL;
    slab->phys_addr = phys_addr;
    
    serial_puts("slab_create: header done, building freelist...\n");

    // Objects after slab header 
    size_t header_size = sizeof(struct slab);
    size_t aligned_header = (header_size + 15) & ~15;
    void *objects_start = (uint8_t *)slab_mem + aligned_header;
    
    serial_puts("slab_create: objects start at offset ");
    serial_put_dec(aligned_header);
    serial_puts(", building freelist for ");
    serial_put_dec(cache->objects_per_slab);
    serial_puts(" objects...\n");

    // Building the freelist
    slab->freelist = objects_start;
    uint8_t *obj = (uint8_t *)objects_start;
    
    for (size_t i = 0; i < cache->objects_per_slab - 1; i++) {
        void **next_ptr = (void **)obj;
        *next_ptr = obj + cache->object_sz;
        obj += cache->object_sz;
    }

    serial_puts("slab_create: setting last object to NULL...\n");
    // The last object points to nil
    void **last = (void **)obj;
    *last = NULL;
    
    serial_puts("slab_create: success!\n");
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
    serial_puts("  Initializing cache for size ");
    serial_put_dec(object_size);
    serial_puts("B...\n");
    
    cache->name = name;
    cache->object_sz = object_size;
    cache->partial = NULL;
    cache->full = NULL;

    serial_puts("    sizeof(struct slab) = ");
    serial_put_dec(sizeof(struct slab));
    serial_puts("\n");
    
    size_t usable_size = 4096 - sizeof(struct slab) - 16;
    serial_puts("    usable_size = ");
    serial_put_dec(usable_size);
    serial_puts("\n");
    
    cache->objects_per_slab = usable_size / object_size;
    
    serial_puts("    objects_per_slab = ");
    serial_put_dec(cache->objects_per_slab);
    serial_puts("\n");
    
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

void test_kalloc(void) {
    serial_puts("\n=== Testing Kernel Allocator ===\n");
    
    // Test 1: Basic allocation
    serial_puts("\nTest 1: Basic allocation\n");
    
    serial_puts("Allocating 64B...\n");
    void *ptr1 = kmalloc(64);
    serial_puts("Got: ");
    serial_put_hex((uint64_t)ptr1);
    serial_puts("\n");
    
    serial_puts("Allocating 128B...\n");
    void *ptr2 = kmalloc(128);
    serial_puts("Got: ");
    serial_put_hex((uint64_t)ptr2);
    serial_puts("\n");
    
    serial_puts("Allocating 256B...\n");
    void *ptr3 = kmalloc(256);
    serial_puts("Got: ");
    serial_put_hex((uint64_t)ptr3);
    serial_puts("\n");
    
    // Test 2: Write and read back
    serial_puts("\nTest 2: Write and read verification\n");
    uint64_t *test_ptr = (uint64_t *)ptr1;
    *test_ptr = 0xDEADBEEFCAFEBABE;
    serial_puts("Wrote 0xDEADBEEFCAFEBABE, read back: ");
    serial_put_hex(*test_ptr);
    serial_puts("\n");
    
    // Test 3: Free and reallocate (should reuse same address)
    serial_puts("\nTest 3: Free and reallocate\n");
    serial_puts("Freeing 128B allocation at ");
    serial_put_hex((uint64_t)ptr2);
    serial_puts("\n");
    kfree(ptr2);
    
    void *ptr4 = kmalloc(128);
    serial_puts("Reallocated 128B at ");
    serial_put_hex((uint64_t)ptr4);
    if (ptr4 == ptr2) {
        serial_puts(" [REUSED - GOOD!]\n");
    } else {
        serial_puts(" [NEW ADDRESS - OK]\n");
    }
    
    // Test 4: Allocate many small objects (stress test)
    serial_puts("\nTest 4: Allocate 100 small objects\n");
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(32);
        if (!ptrs[i]) {
            serial_puts("Failed at allocation ");
            serial_put_dec(i);
            serial_puts("\n");
            break;
        }
    }
    serial_puts("Successfully allocated 100 objects\n");
    
    // Write unique values to each
    for (int i = 0; i < 100; i++) {
        uint32_t *p = (uint32_t *)ptrs[i];
        *p = 0x1000 + i;
    }
    
    // Verify values
    int errors = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t *p = (uint32_t *)ptrs[i];
        if (*p != 0x1000 + i) {
            errors++;
        }
    }
    serial_puts("Verified all values, errors: ");
    serial_put_dec(errors);
    serial_puts("\n");
    
    // Free half of them
    serial_puts("Freeing 50 objects...\n");
    for (int i = 0; i < 100; i += 2) {
        kfree(ptrs[i]);
    }
    
    // Reallocate and verify reuse
    serial_puts("Reallocating 50 objects...\n");
    int reused = 0;
    for (int i = 0; i < 100; i += 2) {
        void *new_ptr = kmalloc(32);
        // Check if we got one of the freed addresses back
        for (int j = 0; j < 100; j += 2) {
            if (new_ptr == ptrs[j]) {
                reused++;
                break;
            }
        }
        ptrs[i] = new_ptr;
    }
    serial_puts("Reused addresses: ");
    serial_put_dec(reused);
    serial_puts(" / 50\n");
    
    // Free everything
    serial_puts("Freeing all 100 objects...\n");
    for (int i = 0; i < 100; i++) {
        kfree(ptrs[i]);
    }
    
    // Test 5: Different sizes in same cache
    serial_puts("\nTest 5: Different sizes, same cache\n");
    void *p16a = kmalloc(10);  // Rounds up to 16
    void *p16b = kmalloc(16);
    void *p32a = kmalloc(17);  // Rounds up to 32
    void *p32b = kmalloc(30);
    
    serial_puts("10B  -> ");
    serial_put_hex((uint64_t)p16a);
    serial_puts("\n16B  -> ");
    serial_put_hex((uint64_t)p16b);
    serial_puts("\n17B  -> ");
    serial_put_hex((uint64_t)p32a);
    serial_puts("\n30B  -> ");
    serial_put_hex((uint64_t)p32b);
    serial_puts("\n");
    
    kfree(p16a);
    kfree(p16b);
    kfree(p32a);
    kfree(p32b);
    
    // Test 6: Slab destruction (allocate and free repeatedly)
    serial_puts("\nTest 6: Slab creation/destruction\n");
    serial_puts("Creating and destroying slabs 10 times...\n");
    
    for (int round = 0; round < 10; round++) {
        void *temp_ptrs[200];
        
        // Allocate enough to create multiple slabs
        for (int i = 0; i < 200; i++) {
            temp_ptrs[i] = kmalloc(64);
        }
        
        // Free all of them (should destroy empty slabs)
        for (int i = 0; i < 200; i++) {
            kfree(temp_ptrs[i]);
        }
        
        serial_puts(".");
    }
    serial_puts(" Done!\n");
    
    // Test 7: Edge cases
    serial_puts("\nTest 7: Edge cases\n");
    
    void *null_alloc = kmalloc(0);
    serial_puts("kmalloc(0) = ");
    serial_put_hex((uint64_t)null_alloc);
    serial_puts(" (should be 0)\n");
    
    kfree(NULL);
    serial_puts("kfree(NULL) - should not crash\n");
    
    void *max_cache = kmalloc(4096);
    serial_puts("kmalloc(4096) = ");
    serial_put_hex((uint64_t)max_cache);
    serial_puts(" (largest cache)\n");
    kfree(max_cache);
    
    void *too_large = kmalloc(8192);
    serial_puts("kmalloc(8192) = ");
    serial_put_hex((uint64_t)too_large);
    serial_puts(" (should be 0 - not supported yet)\n");
    
    serial_puts("\n=== All tests complete! ===\n");
}