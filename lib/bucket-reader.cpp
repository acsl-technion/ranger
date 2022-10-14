#include <sstream>
#include "bucket-reader.h"
#include "hash-methods.h"
#include "simd.h"
#include "perf.h"
#include "record.h"

const int ITERATION_BYTES = SIMD_WIDTH*sizeof(int);

bucket_reader::bucket_reader(bool use_64bit)
:
  use_64bit(use_64bit),
  data(nullptr),
  apdx(nullptr)
{}

bucket_reader::bucket_reader(char *data, char *apdx, bool use_64bit)
:
  use_64bit(use_64bit),
  data(data),
  apdx(apdx)
{}

static inline char *
get_bucket_ptr(char *data, uint64_t bucket_index, bool use_64bit)
{
    return data + (bucket_index * bucket_builder::get_size_bytes(use_64bit));
}

size_t
bucket_reader::get_redundant_bytes(uint64_t idx) const
{
    const int total_bytes = bucket_builder::get_size_bytes(use_64bit);
    uint64_t value64;
    uint32_t value32;
    size_t out;
    int bit;
    int stride;

    out = 0;
    stride = use_64bit ? sizeof(uint64_t) : sizeof(uint32_t);
    for (int i=CACHE_LINE_SIZE; i<total_bytes; i+=stride) {
        if (use_64bit) {
            value64 = *(uint64_t*)(get_bucket_ptr(data, idx, true)+i);
            BSR64(bit, value64);
            bit >>= 4;
            if (!value64 || !bit) out += 6;
            else if (bit == 1) out += 4;
            else if (bit == 2) out += 2;
        } else {
            value32 = *(uint32_t*)(get_bucket_ptr(data, idx, false)+i);
            BSR32(bit, value32);
            bit >>= 4;
            if (!value32 || !bit) out += 2;
        }
    }

    return out;
}

static int
get_bucket_key_num(char *ptr)
{
    EPU_REG bucket;
    EPU_REG zeros = SIMD_SET1_EPI16(0);
    EPU_REG ones = SIMD_SET1_EPI16(0xFFFF);
    uint32_t result, tmp;
    result = 0;

    for (int ofst=0; ofst<CACHE_LINE_SIZE; ofst+=ITERATION_BYTES) {
        bucket = SIMD_LOADU_SI(ptr + ofst);
        SIMD_CMPEQ_EPI32(bucket, bucket, zeros);
        bucket = SIMD_ANDNOT_SI(bucket, ones);
        SIMD_MOVE_MASK_EPI8(tmp, bucket);
        if (tmp) {
            BSR32(tmp, tmp);
            result += (tmp>>1);
        }
    }

    return result;
}

std::vector<bucket_reader::element>
bucket_reader::get_bucket_contents64(char *ptr) const
{
    std::vector<element> out;
    uint16_t *hash_cursor;
    uint64_t *val_cursor;
    element elem;
    int max;

    hash_cursor = (uint16_t*)ptr;
    val_cursor = (uint64_t*)(ptr + CACHE_LINE_SIZE);
    max = get_bucket_key_num(ptr);

    for (int i=0; i<max; ++i) {
        /* LSbit of the hash indicates whether the value is apdx pointer */
        elem.hash = hash_15bit_read(hash_cursor);
        if (*hash_cursor & 1) {
            elem.count = (uint32_t)*val_cursor;
            elem.vals64 = (uint64_t*)(apdx + (*val_cursor >> 32));
        } else {
            elem.count = 1;
            elem.vals64 = val_cursor;
        }
        val_cursor++;
        hash_cursor++;
        out.push_back(elem);
    }
    return out;
}

std::vector<bucket_reader::element>
bucket_reader::get_bucket_contents32(char *ptr) const
{
    std::vector<element> out;
    uint16_t *hash_cursor;
    uint32_t *val_cursor;
    element elem;
    int max;

    hash_cursor = (uint16_t*)ptr;
    val_cursor = (uint32_t*)(ptr + CACHE_LINE_SIZE);
    max = get_bucket_key_num(ptr);

    for (int i=0; i<max; ++i) {
        /* LSbit of the hash indicates whether the value is apdx pointer */
        elem.hash = hash_15bit_read(hash_cursor);
        if (*hash_cursor & 1) {
            elem.count = *(uint32_t*)(apdx + *val_cursor);
            elem.vals32 = ((uint32_t*)(apdx + *val_cursor) + 1);
        } else {
            elem.count = 1;
            elem.vals32 = val_cursor;
        }
        val_cursor++;
        hash_cursor++;
        out.push_back(elem);
    }
    return out;
}

std::string
bucket_reader::get_bucket_string(uint64_t bkt_idx, uint64_t base_range) const
{
    std::vector<element> bcv;
    std::stringstream ss;
    char *ptr;

    ptr = get_bucket_ptr(data, bkt_idx, use_64bit);
    bcv = use_64bit ? get_bucket_contents64(ptr) : get_bucket_contents32(ptr);

    for (int i=0; i<get_bucket_key_num(ptr); i++) {
        ss << bcv[i].hash << " (" << bcv[i].count << ") ";
    }
    ss << std::endl;
    return ss.str();
}

std::vector<uint32_t>
bucket_reader::get_occurence_list(uint64_t bkt_idx, uint64_t base_range) const
{
    std::vector<uint32_t> out;
    std::vector<element> bcv;
    char *ptr;

    ptr = get_bucket_ptr(data, bkt_idx, use_64bit);
    bcv = use_64bit ? get_bucket_contents64(ptr) : get_bucket_contents32(ptr);
    for (auto & it : bcv) {
        out.push_back(it.count);
    }

    return out;
}

std::string
bucket_reader::get_key_values(uint64_t bkt_idx,
                              uint64_t base_range,
                              uint64_t key) const
{
    std::vector<element> bcv;
    std::stringstream ss;
    uint16_t hash;
    bool found;
    char *ptr;

    hash = hash_15bit_key(key, base_range);
    ptr = get_bucket_ptr(data, bkt_idx, use_64bit);
    bcv = use_64bit ? get_bucket_contents64(ptr) : get_bucket_contents32(ptr);
    found = false;

    for (auto & it : bcv) {
        if (it.hash != hash) {
            continue;
        }
        ss << "Found (" << it.count << "): ";
        found = true;
        for (uint32_t j=0; j<it.count; ++j) {
            if (use_64bit) {
                ss << it.vals64[j] << " ";
            } else {
                ss << it.vals32[j] << " ";
            }
        }
    }

    if (!found) {
        ss << "Not found.";
    }

    return ss.str();
}

void
bucket_reader::lookup_batch(const std::array<uint64_t, N> &keys,
                            const std::array<int, N> &search_results,
                            std::array<uint64_t, N> &base_ranges,
                            std::array<int, N> &num,
                            std::array<char*, N> &ptr) const
{
    EPU_REG result, hash_reg, phashes, hashmask;
    uint16_t* hash_ptr;
    uint64_t fullmask;
    uint32_t mask;
    uint16_t hash;
    char *bucket;

    /* Used to switch off the LSbit in hash */
    hashmask = SIMD_SET1_EPI16(0xFFFE);

    /* Prefetch into L2 */
    for (int i=0; i<N; ++i) {
        bucket = get_bucket_ptr(data, search_results[i], use_64bit);
        __builtin_prefetch(bucket, 0, 1);
        __builtin_prefetch(bucket+CACHE_LINE_SIZE, 0, 0);
        __builtin_prefetch(bucket+2*CACHE_LINE_SIZE, 0, 0);
        __builtin_prefetch(bucket+3*CACHE_LINE_SIZE, 0, 0);
    }

    /* Fill "locs" for each input. One cache line access per key */
    for (int i=0; i<N; ++i) {
        fullmask = 0;

        bucket = get_bucket_ptr(data, search_results[i], use_64bit);

        hash = hash_15bit_key(keys[i], base_ranges[i]);
        hash_reg = SIMD_SET1_EPI16(hash);

        /* Populate "fullmask" with 0b11 per match */
        for (int ofst=0; ofst<CACHE_LINE_SIZE; ofst+=ITERATION_BYTES) {
            /* Load bucket hashes from index at "cursor" */
            phashes = SIMD_LOADU_SI(bucket+ofst);
            phashes = SIMD_AND_SI(phashes, hashmask);
            /* "results" holds 0xffff for matched locations */
            SIMD_CMPEQ_EPI16(result, hash_reg, phashes);
            /* Mask holds 0b11 for matched locations */
            SIMD_MOVE_MASK_EPI8(mask, result);
            fullmask |= ((uint64_t)mask<<ofst);
        }

        /* No match */
        if (!fullmask) {
            num[i] = 0;
            continue;
        }

        /* Get first match (lowest to greatest, little endian) */
        BSF64(fullmask, fullmask);
        /* sizeof(uint32_t) * fullmask == sizeof(uint64_t) * fullmask / 2 */
        ptr[i] = bucket + CACHE_LINE_SIZE + sizeof(uint32_t) * fullmask;
        /* "fullmask" has twice the value than what's found, which is okay
         * since we want uint16_t */
        hash_ptr = (uint16_t*)(bucket + fullmask);
        /* Handle singletons */
        if (!(*hash_ptr & 1)) {
            num[i] = 1;
        }
        /* Handle 64bit appendix */
        else if (use_64bit) {
            fullmask = *(uint64_t*)ptr[i];
            num[i] = (uint32_t)fullmask;
            ptr[i] = apdx + (fullmask >> 32);
            __builtin_prefetch(ptr[i], 0, 1);
        }
        /* Handle 32bit appendix */
        else {
            fullmask = *(uint32_t*)ptr[i];
            num[i] = *(uint32_t*)(apdx + fullmask);
            ptr[i] = (char*)((uint32_t*)(apdx + fullmask) + 1);
            __builtin_prefetch(ptr[i], 0, 1);
        }
    }
}
