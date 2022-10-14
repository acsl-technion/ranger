#ifndef DB_BUILDER_H
#define DB_BUILDER_H

#include <vector>
#include <list>
#include <cstdio>
#include "appendix.h"
#include "binstream.h"
#include "callback-message.h"
#include "libnuevomatchup.h"
#include "record.h"
#include "bucket-builder.h"

class db_builder {
public:

    enum { DB_BUILD, START_TRAINING, DONE_TRAINING };

    /* Sent to callback method with statistics */
    struct status {
        int build_percent;
        int status;
        const int *model_errors;
        size_t model_error_num;
    };

    using callback_type = callback_message<db_builder, struct status>;

private:
    struct lnmu_rangearr *rangearr;
    struct lnmu_rqrmi64 *rqrmi;
    std::vector<uint64_t> ranges;
    std::vector<int> rqrmi_size;
    std::vector<uint8_t> prefix_bits;
    mem_binstream *mstream;
    binstream *bstream;
    callback_type callback;
    int compression;
    bool use_64bit;
    size_t distinct_key_num;
    size_t bucket_num;
    size_t used_bytes;
    size_t singleton_num;
    size_t total_key_num;
    appendix apdx;

public:

    db_builder(bool use_64bit);
    db_builder(const db_builder&) = delete;
    ~db_builder();

    void clear();

    /* Returns the DB size in bytes */
    size_t get_db_size() const;

    /* Build database */
    void build(size_t record_num,
               next_record_func_t get_next,
               void *args);

    /* Returns a number in [0,1] on how the db is utilized.
     * 1 stands for 100% utilization. */
    double get_utilization() const;

    /* Set range compression value */
    void set_compression(int val);

    /* Set callback method for this */
    callback_type& on_update();

    /* Custom model size */
    void set_model_size(std::vector<int> size);

    /* Returns the ranges of the DB */
    const std::vector<uint64_t>& get_ranges() const;

    /* Returns the appendix of this */
    const appendix& get_appendix() const;

    /* Returns the singleton percent of this (in [0,1]) */
    double get_singleton_percent() const;

    /* Returns the number of distinct keys in this */
    size_t get_disctinct_key_num() const;

    /* Returns the range-array compression ratio */
    int get_compression() const;

    /* Returns the number of ranges for model training (after compression) */
    size_t get_range_num() const;

    /* Build the model. Returns 0 on success. */
    int build_model();

    /* Write this to file */
    binstream& write(binstream&);

private:

    void add_bucket(bucket_builder *bucket_b, char *blob);
    void update_stats(bucket_builder *bucket_b);
};


#endif
