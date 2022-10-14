#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include "bucket-builder.h"
#include "hash-methods.h"
#include "simd.h"

#define MAX_KEYS_IN_PAGE 32

bucket_builder::attr::attr()
: count(0),
  hash(0),
  saved_val64(0),
  saved_val32(0)
{ }

bucket_builder::bucket_builder(bool use_64bit)
: use_64bit(use_64bit),
  smallest_key(0)
{ }

bucket_builder::~bucket_builder()
{
    for (auto &it : keys) {
        delete it.second;
    }
}

void
bucket_builder::clear()
{
    smallest_key = 0;
    for (auto &it : keys) {
        delete it.second;
    }
    keys.clear();
}

bucket_builder::attr *
bucket_builder::get_key_attr(uint64_t key)
{
    struct attr *key_attr;
    auto it = keys.find(key);
    if ((it == keys.end()) && (keys.size() >= MAX_KEYS_IN_PAGE)) {
        return nullptr;
    } else if (it == keys.end()) {
        key_attr = new attr();
        keys[key] = key_attr;
        return key_attr;
    } else {
        return it->second;
    }
}

int
bucket_builder::populate_record(struct record *m, struct attr *key_attr)
{
    uint16_t hash;

    /* Issues may arise only for new keys: must check for hash collision */
    if (!key_attr->count) {
        hash = hash_15bit_key(m->key, smallest_key);

        for (auto &it : keys) {
            if ((it.second != key_attr) &&
                (hash_15bit_read(&it.second->hash) == hash)) {
                return 1;
            }
        }
        key_attr->hash = hash;
    }

    key_attr->count++;
    if (use_64bit) {
        key_attr->values64.push_back(m->value);
        key_attr->saved_val64 = key_attr->values64[0];
    } else {
        key_attr->values32.push_back((uint32_t)m->value);
        key_attr->saved_val32 = key_attr->values32[0];
    }
    return 0;
}

int
bucket_builder::try_insert(struct record *m)
{
    struct attr *key_attr;

    key_attr = get_key_attr(m->key);

    /* Can't put in more than maximum */
    if (key_attr == nullptr) {
        return 1;
    }

    /* Returns success if there exists a valid hash function */
    if (!populate_record(m, key_attr)) {
        return 0;
    }

    /* Delete key attributes */
    delete key_attr;
    keys.erase(m->key);
    return 1;
}

int
bucket_builder::key_sort(const void *pa, const void *pb, void *pargs)
{
    const uint64_t a = *(uint64_t*)pa;
    const uint64_t b = *(uint64_t*)pb;
    sortargs *args = (sortargs*)pargs;

    /* Singletons come before appendix */
    if (args->keys->at(a)->count < args->keys->at(b)->count) {
        return true;
    }
    /* Singletons are sorted by their values */
    return args->use_64bit ?
           (args->keys->at(a)->values64[0] < args->keys->at(b)->values64[0]) :
           (args->keys->at(a)->values32[0] < args->keys->at(b)->values32[0]);
}

std::vector<uint64_t>
bucket_builder::get_key_order()
{
    sortargs args;
    std::vector<uint64_t> out;

    args.keys = &keys;
    args.use_64bit = use_64bit;

    out.reserve(keys.size());
    for (auto & it : keys) {
        out.push_back(it.first);
    }
    qsort_r(&out[0], out.size(), sizeof(uint64_t), key_sort, &args);
    return out;
}

size_t
bucket_builder::get_size_bytes(bool use_64bit)
{
    return use_64bit ?
           320 : /* 64B index + 4 * 64B = 256B */
           192 ; /* 64B index + 2 * 64B = 128B */
}

size_t
bucket_builder::get_distinct_key_num() const
{
    return keys.size();
}

size_t
bucket_builder::get_total_key_num() const
{
    size_t count = 0;
    for (auto &it : keys) {
        count += it.second->count;
    }
    return count;
}

int
bucket_builder::push(struct record *m)
{
    if (!keys.size()) {
        smallest_key = m->key;
    }
    return try_insert(m);
}

uint64_t
bucket_builder::get_smallest_key() const
{
    return smallest_key;
}

uint8_t
bucket_builder::get_common_prefix_bits() const
{
    uint64_t largest_key = smallest_key;
    for (auto it : keys) {
        if (it.first > largest_key) {
            largest_key = it.first;
        }
    }

    uint64_t diff = largest_key - smallest_key;
    if (diff == 0) {
        return 64;
    }

    int result;
    BSR64(result, diff);

    return 63 - result;
}

const std::vector<uint64_t>*
bucket_builder::get_key_values64(uint64_t key) const
{
    auto it = keys.find(key);
    if (it == keys.end()) {
        return nullptr;
    }
    return &it->second->values64;
}

const std::vector<uint32_t>*
bucket_builder::get_key_values32(uint64_t key) const
{
    auto it = keys.find(key);
    if (it == keys.end()) {
        return nullptr;
    }
    return &it->second->values32;
}

size_t
bucket_builder::get_used_bytes() const
{
    size_t num = keys.size();
    return num * 10; /* 320B max */
}

bool
bucket_builder::get_use_64bit() const
{
    return use_64bit;
}

size_t
bucket_builder::get_singleton_num() const
{
    size_t count = 0;
    for (auto &it : keys) {
        count += (it.second->count == 1);
    }
    return count;
}

void
bucket_builder::populate_appendix(appendix &a)
{
    /* Add large records to the appendix */
    for (auto &it : keys) {
        if (it.second->count<=1) {
            continue;
        }
        if (use_64bit) {
            it.second->saved_val64 = a.add_element64(it.second->values64);
        } else {
            it.second->saved_val32 = a.add_element32(it.second->values32);
        }
        it.second->hash |= 1; /* LSBit means value is pointer */
    }
}

void
bucket_builder::pack(char *ptr)
{
    std::vector<uint64_t> order;
    uint16_t  *hash_cursor;
    uint64_t *val64_cursor;
    uint32_t *val32_cursor;

    memset(ptr, 0, get_size_bytes(use_64bit));

    hash_cursor = (uint16_t*)ptr;
    val64_cursor = (uint64_t*)(ptr + CACHE_LINE_SIZE);
    val32_cursor = (uint32_t*)(ptr + CACHE_LINE_SIZE);
    order = get_key_order();

    /* Put hashes and values */
    for (uint64_t k : order) {
        *hash_cursor = keys[k]->hash;
        if (use_64bit) {
            *val64_cursor = keys[k]->saved_val64;
            val64_cursor++;
        } else {
            *val32_cursor = keys[k]->saved_val32;
            val32_cursor++;
        }
        hash_cursor++;
    }
}
