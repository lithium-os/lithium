#include <stddef.h>

#include "serial.h"
#include "limine.h"

// Limine base revision
__attribute__((used, section(".requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);

// Memory map request
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

void _start(void) {
    serial_init();
    serial_puts("Welcome to Lithium!\n");
    
    if (memmap_request.response == NULL) {
        serial_puts("PANIC: No memory map!\n");
        for (;;) asm("hlt");
    }
    
    serial_puts("Memory map received!\n");
    
    for (;;) asm("hlt");
}