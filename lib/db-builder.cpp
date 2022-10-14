#include <cmath>
#include <set>
#include "db-builder.h"
#include "hash-methods.h"
#include "simd.h"

db_builder::db_builder(bool use_64bit)
:rangearr(nullptr),
 rqrmi(nullptr),
 mstream(new mem_binstream),
 bstream(new binstream(*mstream)),
 compression(1),
 use_64bit(use_64bit),
 distinct_key_num(0),
 bucket_num(0),
 used_bytes(0),
 singleton_num(0),
 total_key_num(0)
{ }

db_builder::~db_builder()
{
    delete mstream;
    delete bstream;
    lnmu_range_array_destroy(rangearr);
    lnmu_rqrmi64_destroy(rqrmi);
}

void
db_builder::clear()
{
    ranges.clear();
    rqrmi_size.clear();
    prefix_bits.clear();
    used_bytes = 0;
    distinct_key_num = 0;
    singleton_num = 0;
    total_key_num = 0;
    lnmu_range_array_destroy(rangearr);
    lnmu_rqrmi64_destroy(rqrmi);
    rqrmi = nullptr;
    rangearr = nullptr;
    delete mstream;
    delete bstream;
    mstream = new mem_binstream;
    bstream = new binstream(*mstream);
}

size_t
db_builder::get_db_size() const
{
    return bucket_num * bucket_builder::get_size_bytes(use_64bit);
}

void
db_builder::set_compression(int value)
{
    compression = value;
}

int
db_builder::get_compression() const
{
    return compression;
}

size_t
db_builder::get_range_num() const
{
    return ranges.size() / compression;
}

db_builder::callback_type &
db_builder::on_update()
{
    return this->callback;
}

/* Returns the ranges of the DB */
const std::vector<uint64_t>&
db_builder::get_ranges() const
{
    return ranges;
}

double
db_builder::get_singleton_percent() const
{
    return (double)singleton_num / distinct_key_num;
}

size_t
db_builder::get_disctinct_key_num() const
{
    return distinct_key_num;
}

void
db_builder::add_bucket(bucket_builder *bucket_b, char *blob)
{
    bucket_b->populate_appendix(apdx);
    ranges.push_back(bucket_b->get_smallest_key());
    bucket_b->pack(blob);
    bucket_num++;
}

void
db_builder::update_stats(bucket_builder *bucket_b)
{
    used_bytes += bucket_b->get_used_bytes();
    singleton_num += bucket_b->get_singleton_num();
    distinct_key_num += bucket_b->get_distinct_key_num();
    total_key_num += bucket_b->get_total_key_num();
    prefix_bits.push_back(bucket_b->get_common_prefix_bits());
}

void
db_builder::build(size_t record_num,
                  next_record_func_t get_next,
                  void *args)
{
    bucket_builder bucket_b(use_64bit);
    struct record m;
    struct record m_last;
    int percent, last;
    int retval;
    char *blob;

    clear();
    last = -1;
    blob = new char[bucket_builder::get_size_bytes(use_64bit)];

    for (size_t i=0; i<record_num; ++i) {
        percent = 100*i/record_num;
        if (percent > last) {
            last = percent;
            callback.msg.status = DB_BUILD;
            callback.msg.build_percent = percent;
            callback.publish(*this);
        }

        /* Get next record */
        m_last = m;
        retval = get_next(&m, args);
        if (retval) {
            break;
        }

        /* Current record is successful pushed into the current bucket */
        if (!bucket_b.push(&m)) {
            continue;
        }

        add_bucket(&bucket_b, blob);
        bstream->write(blob, bucket_builder::get_size_bytes(use_64bit));
        update_stats(&bucket_b);
        bucket_b.clear();
        bucket_b.push(&m);
    }

    /* If last bucket is not empty */
    if (bucket_b.get_used_bytes()) {
        add_bucket(&bucket_b, blob);
        bstream->write(blob, bucket_builder::get_size_bytes(use_64bit));
        update_stats(&bucket_b);
    }

    delete[] blob;
    callback.msg.build_percent = 100;
    callback.publish(*this);
}

double
db_builder::get_utilization() const
{
    return (double)used_bytes / get_db_size();
}

const appendix&
db_builder::get_appendix() const
{
    return apdx;
}

void
db_builder::set_model_size(std::vector<int> size)
{
    rqrmi_size = size;
}

static std::vector<int>
model_size(size_t range_num)
{
    if (range_num < 1000) {
        return {1};
    } else if (range_num < 10000) {
        return {1, 8};
    } else if (range_num < 100000) {
        return {1,8,55};
    } else {
        return {1,8,119};
    }
}

int
db_builder::build_model()
{
    struct lnmu_trainer_configuration pol;
    std::vector<int> rqsize;
    const uint64_t *values;
    int retval;
    size_t size;

    /* Clean previous version */
    lnmu_range_array_destroy(rangearr);
    lnmu_rqrmi64_destroy(rqrmi);

    /* Select RQRMI size according to the number of ranges */
    rangearr = lnmu_range_array_init(&ranges[0],
                                     ranges.size(),
                                     compression,
                                     false);
    size = lnmu_range_array_get_size(rangearr);
    values = lnmu_range_array_get_values(rangearr);

    rqsize = rqrmi_size;
    if (!rqsize.size()) {
        rqsize = model_size(size);
    }

    pol.error_threshold = 64;
    pol.allow_failure = false;
    pol.use_hybrid = true;
    pol.use_batching = true;
    pol.samples = 16e3;
    pol.max_sessions = 20;

    callback.msg.status = START_TRAINING;
    callback.publish(*this);

    rqrmi = lnmu_rqrmi64_init(&pol, &rqsize[0], rqsize.size());
    retval =  lnmu_rqrmi64_train(rqrmi, values, size);

    callback.msg.status = DONE_TRAINING;
    callback.msg.model_errors = lnmu_rqrmi64_get_errors(rqrmi,
                                &callback.msg.model_error_num);
    callback.publish(*this);
    return retval;
}

binstream&
db_builder::write(binstream& s)
{
    size_t apdx_size;
    size_t size;
    double prefix_bits_mean;
    double prefix_bits_stddev;
    double value;
    char *blob;

    /* Calculate total size */
    apdx_size = apdx.get_size();
    size = get_db_size() + apdx_size;

    s.write_header("db", 1);
    s << size
      << use_64bit
      << apdx_size
      << bucket_num
      << compression;

    /* Write statistics */
    s << total_key_num
      << distinct_key_num
      << singleton_num
      << used_bytes;

    /* Calculate prefix bits statistics */
    prefix_bits_mean = 0;
    prefix_bits_stddev = 0;
    for (uint8_t it : prefix_bits) {
        prefix_bits_mean += it;
    }
    prefix_bits_mean /= prefix_bits.size();
    for (uint8_t it : prefix_bits) {
        value = ((double)it - prefix_bits_mean);
        prefix_bits_stddev += value * value;
    }
    prefix_bits_stddev = std::sqrt(prefix_bits_stddev / prefix_bits.size());

    s << prefix_bits_mean
      << prefix_bits_stddev;

    /* Pack buckets */
    s.write("blb", 4);
    blob = (char*)mstream->detach_data(&size);
    s.write(blob, size);
    free(blob);

    /* Pack appendix */
    s.write(apdx.get_data(), apdx_size);

    /* Pack ranges, RQRMI model */
    s << ranges;

    lnmu_rqrmi64_store(rqrmi, (void**)&blob, &size);
    s << size;
    s.write(blob, size);
    free(blob);

    return s;
}
