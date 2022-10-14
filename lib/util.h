#ifndef UTIL_H
#define UTIL_H

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include "errno.h"

/* Returns true if X is a power of 2, otherwise false. */
#define IS_POW2(X) ((X) && !((X) & ((X) - 1)))
/* Returns X / Y, rounding up.  X must be nonnegative to round correctly. */
#define DIV_ROUND_UP(X, Y) (((X) + ((Y) - 1)) / (Y))
/* Returns X rounded up to the nearest multiple of Y. */
#define ROUND_UP(X, Y) (DIV_ROUND_UP(X, Y) * (Y))
/* Returns the least number that, when added to X, yields a multiple of Y. */
#define PAD_SIZE(X, Y) (ROUND_UP(X, Y) - (X))

void abort_msg(const char *msg);
void * xmalloc(size_t size);
void * xmalloc_size_align(size_t size, size_t alignment);
void * xmalloc_cacheline(size_t size);
void free_size_align(void *p);
void free_cacheline(void *p);

#endif
