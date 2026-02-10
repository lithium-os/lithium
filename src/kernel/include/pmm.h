#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "limine.h"

void pmm_init(struct limine_memmap_response *memmap, uint64_t _hhdm_offset);
void *pmm_alloc(void);
void pmm_free(void *ptr);

#endif