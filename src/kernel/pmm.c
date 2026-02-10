// kernel/src/pmm.c
#include <stddef.h>
#include <stdint.h>
#include "include/limine.h"
#include "include/serial.h"
#include "include/pmm.h"

#define PAGE_SIZE 4096

// Free page node - each free page points to the next
typedef struct free_page {
    struct free_page *next;
} free_page_t;

static free_page_t *free_list_head = NULL;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t hhdm_offset = 0;

// Align address down to page boundary
static inline uint64_t align_down(uint64_t addr) {
    return addr & ~(PAGE_SIZE - 1);
}

// Align address up to page boundary
static inline uint64_t align_up(uint64_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

// Convert physical addr to virtual addr
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_offset);
}

// Add a memory region to the free list
static void add_region(uint64_t base, uint64_t length) {
    uint64_t page_aligned_base = align_up(base);
    uint64_t page_aligned_end = align_down(base + length);
    
    if (page_aligned_base >= page_aligned_end) {
        return; // Region too small
    }
    
    uint64_t page_count = (page_aligned_end - page_aligned_base) / PAGE_SIZE;
    
    // Add each page to the free list
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t page_addr = page_aligned_base + (i * PAGE_SIZE);
        free_page_t *page = (free_page_t *)phys_to_virt(page_addr);
        
        // Push onto free list
        page->next = free_list_head;
        free_list_head = page;
        free_pages++;
    }
    
    total_pages += page_count;
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t _hhdm_offset) {
    serial_puts("Initializing PMM...\n");
    hhdm_offset = _hhdm_offset;
    
    // Parse memory map and add usable regions
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            serial_puts("  Adding usable region: ");
            serial_put_hex(entry->base);
            serial_puts(" - ");
            serial_put_hex(entry->base + entry->length);
            serial_puts("\n");
            add_region(entry->base, entry->length);
        }
    }
    
    serial_puts("PMM initialized: ");
    serial_put_dec(free_pages);
    serial_puts(" / ");
    serial_put_dec(total_pages);
    serial_puts(" pages free (");
    serial_put_dec((free_pages * PAGE_SIZE) / (1024 * 1024));
    serial_puts(" MB)\n");
}

// Allocate a physical page
void *pmm_alloc(void) {
    if (free_list_head == NULL) {
        serial_puts("PMM: Out of memory!\n");
        return NULL;
    }
    
    free_page_t *page = free_list_head;
    free_list_head = page->next;
    free_pages--;
    
    return (void *)page;
}

// Free a physical page
void pmm_free(void *ptr) {
    if (ptr == NULL) return;
    
    free_page_t *page = (free_page_t *)ptr;
    page->next = free_list_head;
    free_list_head = page;
    free_pages++;
}