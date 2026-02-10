#ifndef VMM_H
#define VMM_H

#include <stdint.h>

void vmm_init(void);
void vmm_walk_address(uint64_t vaddr);
void vmm_dump_pml4(void);

#endif