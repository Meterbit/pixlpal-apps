// Pixlpal compat: an app's memory is its own (it grows up to 4 MB); every capability is the same heap.
#pragma once
#include <stdlib.h>
#include <string.h>
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT  (1 << 12)
#define MALLOC_CAP_DMA      (1 << 3)
static inline void* heap_caps_malloc(size_t n, unsigned){ return malloc(n); }
static inline void* heap_caps_calloc(size_t a, size_t b, unsigned){ return calloc(a, b); }
static inline void* heap_caps_realloc(void* p, size_t n, unsigned){ return realloc(p, n); }
static inline void heap_caps_free(void* p){ free(p); }
static inline size_t heap_caps_get_free_size(unsigned){ return 1024 * 1024; }
static inline size_t heap_caps_get_largest_free_block(unsigned){ return 1024 * 1024; }
