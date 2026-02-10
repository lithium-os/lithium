#include <stdint.h>
#include <stddef.h>
#include "include/serial.h"
#include "include/limine_requests.h"
#include "include/vmm.h"

#define PAGE_SIZE 4096

// Page table entry flags
#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITE     (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_PWT       (1ULL << 3)  // Page-level write-through
#define PTE_PCD       (1ULL << 4)  // Page-level cache disable
#define PTE_ACCESSED  (1ULL << 5)
#define PTE_DIRTY     (1ULL << 6)
#define PTE_HUGE      (1ULL << 7)  // 2MB/1GB page
#define PTE_GLOBAL    (1ULL << 8)
#define PTE_NX        (1ULL << 63) // No execute

#define PTE_GET_ADDR(pte) ((pte) & 0x000FFFFFFFFFF000ULL)

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_request.response->offset);
}

static void dump_pte(const char *level, uint64_t index, uint64_t pte) {
    if (!(pte & PTE_PRESENT)) return;
    
    serial_puts("  ");
    serial_puts(level);
    serial_puts("[");
    serial_put_dec(index);
    serial_puts("] -> ");
    serial_put_hex(PTE_GET_ADDR(pte));
    serial_puts(" [");
    
    if (pte & PTE_PRESENT)  serial_puts("P");
    if (pte & PTE_WRITE)    serial_puts("W");
    if (pte & PTE_USER)     serial_puts("U");
    if (pte & PTE_HUGE)     serial_puts("H");
    if (pte & PTE_NX)       serial_puts("NX");
    
    serial_puts("]\n");
}

// Walk page tables for a given virtual address
void vmm_walk_address(uint64_t vaddr) {
    uint64_t cr3 = read_cr3();
    uint64_t pml4_phys = cr3 & 0x000FFFFFFFFFF000ULL;
    
    // Extract indices from virtual address
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx   = (vaddr >> 12) & 0x1FF;
    
    serial_puts("\nWalking page tables for virtual address: ");
    serial_put_hex(vaddr);
    serial_puts("\n");
    
    // PML4
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    uint64_t pml4e = pml4[pml4_idx];
    dump_pte("PML4", pml4_idx, pml4e);
    
    if (!(pml4e & PTE_PRESENT)) {
        serial_puts("  -> NOT MAPPED (PML4 not present)\n");
        return;
    }
    
    // PDPT
    uint64_t *pdpt = phys_to_virt(PTE_GET_ADDR(pml4e));
    uint64_t pdpte = pdpt[pdpt_idx];
    dump_pte("PDPT", pdpt_idx, pdpte);
    
    if (!(pdpte & PTE_PRESENT)) {
        serial_puts("  -> NOT MAPPED (PDPT not present)\n");
        return;
    }
    
    if (pdpte & PTE_HUGE) {
        serial_puts("  -> 1GB HUGE PAGE\n");
        return;
    }
    
    // PD
    uint64_t *pd = phys_to_virt(PTE_GET_ADDR(pdpte));
    uint64_t pde = pd[pd_idx];
    dump_pte("PD  ", pd_idx, pde);
    
    if (!(pde & PTE_PRESENT)) {
        serial_puts("  -> NOT MAPPED (PD not present)\n");
        return;
    }
    
    if (pde & PTE_HUGE) {
        serial_puts("  -> 2MB HUGE PAGE\n");
        return;
    }
    
    // PT
    uint64_t *pt = phys_to_virt(PTE_GET_ADDR(pde));
    uint64_t pte = pt[pt_idx];
    dump_pte("PT  ", pt_idx, pte);
    
    if (!(pte & PTE_PRESENT)) {
        serial_puts("  -> NOT MAPPED (PT not present)\n");
        return;
    }
    
    uint64_t phys_addr = PTE_GET_ADDR(pte) + (vaddr & 0xFFF);
    serial_puts("  -> MAPPED to physical: ");
    serial_put_hex(phys_addr);
    serial_puts("\n");
}

// Dump summary of all mapped regions in PML4
void vmm_dump_pml4(void) {
    uint64_t cr3 = read_cr3();
    uint64_t pml4_phys = cr3 & 0x000FFFFFFFFFF000ULL;
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    
    serial_puts("\n=== PML4 Table Dump ===\n");
    serial_puts("CR3 (PML4 physical): ");
    serial_put_hex(pml4_phys);
    serial_puts("\n\n");
    
    for (int i = 0; i < 512; i++) {
        if (pml4[i] & PTE_PRESENT) {
            uint64_t vaddr_base = ((uint64_t)i << 39);
            // Sign extend if needed (canonical addresses)
            if (i >= 256) {
                vaddr_base |= 0xFFFF000000000000ULL;
            }
            
            serial_puts("PML4[");
            serial_put_dec(i);
            serial_puts("] -> Virtual range: ");
            serial_put_hex(vaddr_base);
            serial_puts(" - ");
            serial_put_hex(vaddr_base + (1ULL << 39) - 1);
            serial_puts(" -> ");
            serial_put_hex(PTE_GET_ADDR(pml4[i]));
            serial_puts(" [");
            if (pml4[i] & PTE_WRITE) serial_puts("W");
            if (pml4[i] & PTE_USER)  serial_puts("U");
            if (pml4[i] & PTE_NX)    serial_puts("NX");
            serial_puts("]\n");
        }
    }
}

void vmm_init(void) {
    serial_puts("\n=== VMM Debug Info ===\n");
    
    // Dump PML4 overview
    vmm_dump_pml4();
    
    // Walk some interesting addresses
    serial_puts("\n=== Testing Address Translation ===\n");
    
    // Kernel code address
    vmm_walk_address(exec_addr_request.response->virtual_base);
    
    // HHDM address
    vmm_walk_address(hhdm_request.response->offset);
    
    // Try a physical address via HHDM
    vmm_walk_address(hhdm_request.response->offset + 0x100000);
}