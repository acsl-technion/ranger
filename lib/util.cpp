#include "util.h"
#include "simd.h"

void
abort_msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    abort();
}

void *
xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (p == NULL) {
        abort_msg("Out of memory");
    }
    return p;
}

/* Allocates and returns 'size' bytes of memory aligned to 'alignment' bytes.
 * 'alignment' must be a power of two and a multiple of sizeof(void *).
 *
 * Use free_size_align() to free the returned memory block. */
void *
xmalloc_size_align(size_t size, size_t alignment)
{
#ifdef HAVE_POSIX_MEMALIGN
    void *p;
    int error;

    error = posix_memalign(&p, alignment, size ? size : 1);
    if (error != 0) {
        abort_msg("Out of memory");
    }
    return p;
#else
    /* Allocate room for:
     *
     *     - Header padding: Up to alignment - 1 bytes, to allow the
     *       pointer 'q' to be aligned exactly sizeof(void *) bytes before the
     *       beginning of the alignment.
     *
     *     - Pointer: A pointer to the start of the header padding, to allow us
     *       to free() the block later.
     *
     *     - User data: 'size' bytes.
     *
     *     - Trailer padding: Enough to bring the user data up to a alignment
     *       multiple.
     *
     * +---------------+---------+------------------------+---------+
     * | header        | pointer | user data              | trailer |
     * +---------------+---------+------------------------+---------+
     * ^               ^         ^
     * |               |         |
     * p               q         r
     *
     */
    void *p, *r, **q;
    bool runt;

    if (!IS_POW2(alignment) || (alignment % sizeof(void *) != 0)) {
        abort_msg("Invalid alignment");
    }

    p = xmalloc((alignment - 1)
                + sizeof(void *)
                + ROUND_UP(size, alignment));

    runt = PAD_SIZE((uintptr_t) p, alignment) < sizeof(void *);
    /* When the padding size < sizeof(void*), we don't have enough room for
     * pointer 'q'. As a reuslt, need to move 'r' to the next alignment.
     * So ROUND_UP when xmalloc above, and ROUND_UP again when calculate 'r'
     * below.
     */
    r = (void *) ROUND_UP((uintptr_t) p + (runt ? alignment : 0), alignment);
    q = (void **) r - 1;
    *q = p;

    return r;
#endif
}

/* Allocates and returns 'size' bytes of memory aligned to a cache line and in
 * dedicated cache lines.  That is, the memory block returned will not share a
 * cache line with other data, avoiding "false sharing".
 *
 * Use free_cacheline() to free the returned memory block. */
void *
xmalloc_cacheline(size_t size)
{
    return xmalloc_size_align(size, CACHE_LINE_SIZE);
}

void
free_size_align(void *p)
{
#ifdef HAVE_POSIX_MEMALIGN
    free(p);
#else
    if (p) {
        void **q = (void **) p - 1;
        free(*q);
    }
#endif
}

/* Frees a memory block allocated with xmalloc_cacheline() or
 * xzalloc_cacheline(). */
void
free_cacheline(void *p)
{
    free_size_align(p);
}

