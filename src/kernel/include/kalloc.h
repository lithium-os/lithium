#ifndef KALLOC_H
#define KALLOC_H

#include <stddef.h>

struct slab;
struct kmem_cache;

void *kmem_cache_alloc(struct kmem_cache *cache);

void kmem_cache_free(struct kmem_cache *cache, void *ptr);

void kalloc_init(void);

void *kalloc(size_t size);

void kfree(void *ptr);

#endif /* KALLOC_H */