#include <stddef.h>

#include "serial.h"
#include "limine.h"
#include "pmm.h"

static void hcf() {
    for (;;) asm("hlt");
}

// Limine base revision
__attribute__((used, section(".requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);

// Memory map request
__attribute__((used, section(".requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

// Add this request alongside your memmap request
__attribute__((used, section(".requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

void _start(void) {
    serial_init();
    serial_puts("Welcome to Lithium!\n");
    
    if (memmap_request.response == NULL) {
        serial_puts("PANIC: No memory map!\n");
        hcf();
    }

    if (hhdm_request.response == NULL) {
    serial_puts("PANIC: No HHDM!\n");
    hcf();
}
    
    pmm_init(memmap_request.response, hhdm_request.response->offset);
    
    // Test the allocator
    serial_puts("\nTesting PMM allocator...\n");
    void *page1 = pmm_alloc();
    void *page2 = pmm_alloc();
    void *page3 = pmm_alloc();
    
    serial_puts("Allocated page 1: ");
    serial_put_hex((uint64_t)page1);
    serial_puts("\nAllocated page 2: ");
    serial_put_hex((uint64_t)page2);
    serial_puts("\nAllocated page 3: ");
    serial_put_hex((uint64_t)page3);
    serial_puts("\n");
    
    pmm_free(page2);
    serial_puts("Freed page 2\n");
    
    void *page4 = pmm_alloc();
    serial_puts("Allocated page 4: ");
    serial_put_hex((uint64_t)page4);
    serial_puts(" (should be same as page 2)\n");
    
    hcf();
}