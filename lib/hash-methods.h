#ifndef HASH_METHODS_H
#define HASH_METHODS_H

#include <cstdint>
#include "simd.h"

static inline uint32_t
hash_rot(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

/* Murmurhash by Austin Appleby,
 * from https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp
 *
 * The upstream license there says:
 *
 *    MurmurHash3 was written by Austin Appleby, and is placed in the public
 *    domain. The author hereby disclaims copyright to this source code.
 *
 * See hash_words() for sample usage. */

static inline uint32_t mhash_add__(uint32_t hash, uint32_t data)
{
    /* zero-valued 'data' will not change the 'hash' value */
    if (!data) {
        return hash;
    }

    data *= 0xcc9e2d51;
    data = hash_rot(data, 15);
    data *= 0x1b873593;
    return hash ^ data;
}

static inline uint32_t mhash_add(uint32_t hash, uint32_t data)
{
    hash = mhash_add__(hash, data);
    hash = hash_rot(hash, 13);
    return hash * 5 + 0xe6546b64;
}

static inline uint32_t hash_finish(uint64_t hash, uint64_t final)
{
    /* The finishing multiplier 0x805204f3 has been experimentally
     * derived to pass the testsuite hash tests. */
    hash = _mm_crc32_u64(hash, final) * 0x805204f3;
    return hash ^ (uint32_t)hash >> 16; /* Increase entropy in LSBs. */
}

static inline uint32_t hash_add(uint32_t hash, uint32_t data)
{
    return mhash_add(hash, data);
}

static inline uint32_t hash_add64(uint32_t hash, uint64_t data)
{
    return hash_add(hash_add(hash, data), data >> 32);
}

static inline uint32_t hash_uint64_basis(const uint64_t x,
                                         const uint32_t basis)
{
    return hash_finish(hash_add64(basis, x), 8);
}

static inline uint32_t hash_uint64(const uint64_t x)
{
    return hash_uint64_basis(x, 0);
}

static inline uint16_t
hash_15bit_key(uint64_t key, uint64_t base_range)
{
    uint16_t out = (uint16_t)hash_uint64(key - base_range) & 0xFFFE;
    return out ? out : 2; /* Never return zero */
}

static inline uint16_t
hash_15bit_read(void *ptr)
{
    return *(uint16_t*)ptr & 0xFFFE;
}

#endif
