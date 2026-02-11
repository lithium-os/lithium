#include <stdint.h>
#include <stddef.h>
#include "../include/serial.h"
#include "../include/limine_requests.h"
#include "../include/vmm.h"
#include "../include/pmm.h"

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

// Helper: Read CR3 
static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Helper: Converts a PHYS addr to VIRT
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_request.response->offset);
}

// Helper: Dumps a PTE
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

// Helper: Get or create a page table entry
static uint64_t *get_or_create_table(uint64_t *table, uint64_t index, int alloc) {
    uint64_t entry = table[index];

    // The table exists, ret it
    if (entry & PTE_PRESENT)
        return phys_to_virt(PTE_GET_ADDR(entry));

    // Table doesn't exist, we're not allocating it
    if (!alloc)
        return NULL;

    void *new_table_virt = pmm_alloc();
    if (!new_table_virt) {
        serial_puts("VMM: Failed to allocate page table!\n");
        return NULL;
    }

    uint64_t new_table_phys = (uint64_t)new_table_virt - hhdm_request.response->offset;
    uint64_t *new_table_ptr = (uint64_t *)new_table_virt;
    for (int i = 0; i < 512; i++) {
        new_table_ptr[i] = 0;
    }

    table[index] = new_table_phys | PTE_PRESENT | PTE_WRITE;
    return new_table_ptr;
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

// Map a virtual address
int vmm_map(uint64_t v_addr, uint64_t phys, uint64_t flags) {
    uint64_t cr3 = read_cr3();
    uint64_t pml4_phys = cr3 & 0x000FFFFFFFFFF000ULL;

    uint64_t pml4_idx = (v_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (v_addr >> 30) & 0x1FF;
    uint64_t pd_idx   = (v_addr >> 21) & 0x1FF;
    uint64_t pt_idx   = (v_addr >> 12) & 0x1FF;

    // Walk / make pt hierarchy
    uint64_t *pml4 = phys_to_virt(pml4_phys);

    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, 1);
    if (!pdpt)
        return -1;
    
    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, 1);
    if (!pd)
        return -1;
    
    uint64_t *pt = get_or_create_table(pd, pd_idx, 1);
    if (!pt) 
        return -1;

    pt[pt_idx] = (phys & 0x000FFFFFFFFFF000ULL) | flags | PTE_PRESENT;
    asm volatile ("invlpg (%0)" :: "r"(v_addr) : "memory");
    return 0;
}

// Unmap virtual address
int vmm_unmap(uint64_t vaddr) {
    uint64_t cr3 = read_cr3();
    uint64_t pml4_phys = cr3 & 0x000FFFFFFFFFF000ULL;
    
    // Extract indices
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx   = (vaddr >> 12) & 0x1FF;
    
    // Walk page tables (no allocation)
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    if (!(pml4[pml4_idx] & PTE_PRESENT))
        return -1;
    
    uint64_t *pdpt = phys_to_virt(PTE_GET_ADDR(pml4[pml4_idx]));
    if (!(pdpt[pdpt_idx] & PTE_PRESENT))
        return -1;
    
    uint64_t *pd = phys_to_virt(PTE_GET_ADDR(pdpt[pdpt_idx]));
    if (!(pd[pd_idx] & PTE_PRESENT))
        return -1;
    
    uint64_t *pt = phys_to_virt(PTE_GET_ADDR(pd[pd_idx]));
    if (!(pt[pt_idx] & PTE_PRESENT))
        return -1;
    
    // Clear the entry
    pt[pt_idx] = 0;
    
    // Flush TLB
    asm volatile ("invlpg (%0)" :: "r"(vaddr) : "memory");
    
    return 0;
}

void vmm_init(void) {
    serial_puts("VMM initalized (prepared by Limine page tables)\n");
    uint64_t cr3 = read_cr3();
    serial_puts("CR3 (PML4): ");
    serial_put_hex(cr3 & 0x000FFFFFFFFFF000ULL);
    serial_puts("\n");
}