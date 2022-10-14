#ifndef BUCKET_READER_H
#define BUCKET_READER_H

#include <array>
#include <string>
#include <vector>

#include "hash-methods.h"
#include "bucket-builder.h"
#include "libnuevomatchup.h"

class bucket_reader {

    struct element {
        uint64_t *vals64;
        uint32_t *vals32;
        uint32_t count;
        uint16_t hash;
    };

    bool use_64bit;
    char *data;
    char *apdx;

public:

    /* Query batch size */
    static constexpr int N = LNMU_BATCH_SIZE;

    bucket_reader(bool use_64bit = true);
    bucket_reader(char *data, char *apdx, bool use_64bit);

    /* Returns a textual representation of a bucket in this  */
    std::string get_bucket_string(uint64_t idx, uint64_t base_range) const;

    /* Returns a textual representation of the values of a specific key, if
     * found */
    std::string get_key_values(uint64_t bucket_idx,
                               uint64_t base_range,
                               uint64_t key) const;

    /* Returns the number of bytes that hold no data and can be spared */
    size_t get_redundant_bytes(uint64_t idx) const;

    /* Performs a batch lookup of N keys in N buckets. Populates "num" to the
     * number of values per key (0 if not found), and sets "ptr" to point
     * to the value of each key. */
    void lookup_batch(const std::array<uint64_t, N> &keys,
                      const std::array<int, N> &search_results,
                      std::array<uint64_t, N> &base_ranges,
                      std::array<int, N> &num,
                      std::array<char*, N> &ptr) const;

    /* Returns a vector of all key occurrences in this */
    std::vector<uint32_t> get_occurence_list(uint64_t bucket_idx,
                                             uint64_t base_range) const;

private:

    std::vector<element> get_bucket_contents64(char *ptr) const;
    std::vector<element> get_bucket_contents32(char *ptr) const;
};


#endif
