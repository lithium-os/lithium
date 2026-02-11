#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define VMM_PRESENT  (1ULL << 0)
#define VMM_WRITE    (1ULL << 1)
#define VMM_USER     (1ULL << 2)
#define VMM_NX       (1ULL << 63)

void vmm_init(void);
void vmm_walk_address(uint64_t vaddr);
void vmm_dump_pml4(void);

int vmm_map(uint64_t v_addr, uint64_t phys, uint64_t flags);
int vmm_unmap(uint64_t v_addr);

#endif