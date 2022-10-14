#ifndef DB_READER_H
#define DB_READER_H

#include <array>
#include <cstdint>
#include <string>

#include "appendix.h"
#include "binstream.h"
#include "db-builder.h"
#include "libnuevomatchup.h"
#include "record.h"
#include "bucket-builder.h"
#include "bucket-reader.h"

class db_reader {

    size_t bucket_num;
    int compression;
    bool use_64bit;
    char *data;
    char *apdx;
    struct lnmu_rangearr *ranges;
    struct lnmu_rqrmi64 *model;
    bucket_reader preader;
    uint64_t min, max;

    /* Stats */
    size_t total_bytes;
    size_t appendix_bytes;
    size_t distinct_key_num;
    size_t used_bytes;
    size_t singleton_num;
    size_t total_key_num;
    double prefix_bits_mean;
    double prefix_bits_stddev;

    /* Perf stats */
    double stats_inference;
    double stats_search;
    double stats_validate;
    double stats_lookup;
    double stats_counter;

public:

    /* Query batch size */
    static constexpr int N = LNMU_BATCH_SIZE;

    db_reader();
    db_reader(const db_reader&) = delete;
    db_reader(db_reader&&);
    ~db_reader();

    /* Read content from binstream. Returns 0 on success. */
    int read(binstream&);

    /* For each i in [1..N]: Query keys[i], set num[i] to be the number of
     * matched values, and ptr[i] to point to the data. */
    void query(std::array<uint64_t, N> keys,
               std::array<int, N> &num,
               std::array<char*, N> &ptr);

    void query_perf(std::array<uint64_t, N> keys,
                    std::array<int, N> &num,
                    std::array<char*, N> &ptr);

    /* Returns a debug string for querying "key" */
    std::string debug(uint64_t key) const;

    /* Returns true iff "value" is in the appendix */
    bool is_in_appendix(void *value) const;

    /* Returns the number of distinct keys in this */
    size_t get_distinct_key_num() const;

    /* Returns the total nubmer of keys in this */
    size_t get_total_key_num() const;

    /* Returns the total bytes used by this */
    size_t get_total_bytes() const;

    /* Returns the number of bytes in the appendix */
    size_t get_appendix_bytes() const;

    /* Returns the number of used bytes */
    size_t get_used_bytes() const;

    /* Return number of singletons */
    size_t get_singleton_num() const;

    /* Returns the number of ranges after compression */
    size_t get_range_num() const;

    /* Returns the number of buckets */
    size_t get_bucket_num() const;

    /* Returns the number of bytes that hold no data and can be spared */
    size_t get_redundant_bytes() const;

    /* Returns statistics on prefix bits */
    double get_prefix_bits_mean() const;
    double get_prefix_bits_stddev() const;

    /* Returns a pointer to the ranges the the model is trained with */
    const uint64_t *get_ranges() const;

    /* Return a sorted list of all key occurrences */
    std::vector<uint32_t> get_occurence_list() const;

    /* Returns true iff this uses 64bit values */
    bool get_use_64bit() const;

    /* Get average perf stats */
    double get_stats_inference_ns() const;
    double get_stats_search_ns() const;
    double get_stats_validate_ns() const;
    double get_stats_lookup_ns() const;
};

#endif
