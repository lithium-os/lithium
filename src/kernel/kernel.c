#include <stddef.h>

/**
 * Since this is the kernel entrypoint for Lithium, it should always stay
 * at the top level of the `src/kernel` source tree. Please do not move
 * this file!
 * 
 * -> wellbutteredtoast - 10 Feb 2026
 */

#include "include/serial.h"
#include "include/limine.h"
#include "include/pmm.h"
#include "include/vmm.h"
#include "include/limine_requests.h"
#include "include/kalloc.h"

static void hcf() {
    for (;;) asm("hlt");
}

void _start(void) {
    serial_init();
    serial_puts("\nWelcome to Lithium!\n");
    
    if (!memmap_request.response || !hhdm_request.response || !exec_addr_request.response) {
        serial_puts("PANIC: Missing responses!\n");
        hcf();
    }

    serial_puts("\n === Lithium Kernel Memory Layout === \n");
    serial_puts("HHDM offset:          ");
    serial_put_hex(hhdm_request.response->offset);
    serial_puts("\n");
    
    serial_puts("Kernel physical base: ");
    serial_put_hex(exec_addr_request.response->physical_base);
    serial_puts("\n");
    
    serial_puts("Kernel virtual base:  ");
    serial_put_hex(exec_addr_request.response->virtual_base);
    serial_puts("\n\n");
    
    pmm_init(memmap_request.response, hhdm_request.response->offset);
    vmm_init();
    kalloc_init();

    void *ktptr1 = kmalloc((size_t)128);
    void *ktptr2 = kmalloc((size_t)2048);
    
    kfree(ktptr2);
    kfree(ktptr1);

    hcf();
}