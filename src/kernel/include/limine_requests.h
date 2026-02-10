#ifndef LIMINE_REQUESTS_H
#define LIMINE_REQUESTS_H

#include "limine.h"

extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_executable_address_request exec_addr_request;

#endif /* LIMINE_REQUESTS_H */