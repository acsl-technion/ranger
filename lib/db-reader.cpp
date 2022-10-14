#include <algorithm>
#include <cstddef>
#include <cstring>
#include <sstream>
#include "bucket-builder.h"
#include "db-reader.h"
#include "util.h"
#include "perf.h"

static constexpr int N = db_reader::N;

db_reader::db_reader()
 : bucket_num(0),
   compression(1),
   use_64bit(true),
   data(NULL),
   apdx(NULL),
   ranges(nullptr),
   model(nullptr),
   min(0),
   max(0),
   total_bytes(0),
   appendix_bytes(0),
   distinct_key_num(0),
   used_bytes(0),
   singleton_num(0),
   total_key_num(0),
   prefix_bits_mean(0),
   prefix_bits_stddev(0),
   stats_inference(0),
   stats_search(0),
   stats_validate(0),
   stats_lookup(0),
   stats_counter(0)
{
}

db_reader::db_reader(db_reader &&other)
 : bucket_num(other.bucket_num),
   compression(other.compression),
   use_64bit(other.use_64bit),
   data(other.data),
   apdx(other.apdx),
   ranges(other.ranges),
   model(other.model),
   min(other.min),
   max(other.max),
   total_bytes(other.total_bytes),
   appendix_bytes(other.appendix_bytes),
   distinct_key_num(other.distinct_key_num),
   used_bytes(other.used_bytes),
   singleton_num(other.singleton_num),
   total_key_num(other.total_key_num),
   prefix_bits_mean(other.prefix_bits_mean),
   prefix_bits_stddev(other.prefix_bits_stddev),
   stats_inference(0),
   stats_search(0),
   stats_validate(0),
   stats_lookup(0),
   stats_counter(0)
{
    other.data = nullptr;
    other.model = nullptr;
    other.ranges = nullptr;
}

db_reader::~db_reader()
{
    free_cacheline(data);
    lnmu_rqrmi64_destroy(model);
    lnmu_range_array_destroy(ranges);
}

size_t
db_reader::get_range_num() const
{
    return lnmu_range_array_get_size(ranges);
}

size_t
db_reader::get_bucket_num() const
{
    return bucket_num;
}

size_t
db_reader::get_redundant_bytes() const
{
    size_t out = 0;
    for (size_t i=0; i<bucket_num; ++i) {
        out += preader.get_redundant_bytes(i);
    }
    return out;
}

double
db_reader::get_prefix_bits_mean() const
{
    return prefix_bits_mean;
}

double
db_reader::get_prefix_bits_stddev() const
{
    return prefix_bits_stddev;
}

const uint64_t *
db_reader::get_ranges() const
{
    return lnmu_range_array_get_values(ranges);
}

size_t
db_reader::get_distinct_key_num() const
{
    return distinct_key_num;
}

size_t
db_reader::get_total_key_num() const
{
    return total_key_num;
}

size_t
db_reader::get_total_bytes() const
{
    return total_bytes;
}

size_t
db_reader::get_appendix_bytes() const
{
    return appendix_bytes;
}

size_t
db_reader::get_used_bytes() const
{
    return used_bytes;
}

size_t
db_reader::get_singleton_num() const
{
    return singleton_num;
}

double
db_reader::get_stats_inference_ns() const
{
    return stats_counter ? stats_inference/stats_counter/N : 0;
}

double
db_reader::get_stats_search_ns() const
{
    return stats_counter ? stats_search/stats_counter/N : 0;
}

double
db_reader::get_stats_validate_ns() const
{
    return stats_counter ? stats_validate/stats_counter/N : 0;
}

double
db_reader::get_stats_lookup_ns() const
{
    return stats_counter ? stats_lookup/stats_counter/N : 0;
}

bool
db_reader::is_in_appendix(void *value) const
{
    return value > (void*)apdx;
}

bool
db_reader::get_use_64bit() const
{
    return use_64bit;
}

std::vector<uint32_t>
db_reader::get_occurence_list() const
{
    std::vector<uint32_t> vec;
    for (size_t i=0; i<bucket_num; i++) {
        std::vector<uint32_t> current = preader.get_occurence_list(i, 0);
        vec.insert(vec.end(), current.begin(), current.end());
    }
    std::sort(vec.begin(), vec.end());
    return vec;
}

int
db_reader::read(binstream &s)
{
    std::vector<uint64_t> rlst;
    char blob[4];
    char *buffer;
    size_t size;
    int version;

    version = s.read_header("db");
    if (version != 1) {
        return 1;
    }

    s >> size
      >> use_64bit
      >> appendix_bytes
      >> bucket_num
      >> compression;

    total_bytes = size;

    /* Read statistics */
    s >> total_key_num
      >> distinct_key_num
      >> singleton_num
      >> used_bytes
      >> prefix_bits_mean
      >> prefix_bits_stddev;

    data = (char*)xmalloc_cacheline(size);
    apdx = data + bucket_builder::get_size_bytes(use_64bit) * bucket_num;

    /* Read data blob */
    s.read(blob, 4);
    if (strcmp(blob, "blb")) {
        free_cacheline(data);
        return 1;
    }
    s.read(data, size);

    /* Read ranges */
    s >> rlst;
    min = rlst.front();
    max = rlst.back();
    ranges = lnmu_range_array_init(&rlst[0], rlst.size(), compression, false);

    total_bytes += rlst.size() * sizeof(uint64_t);
    used_bytes += rlst.size() * sizeof(uint64_t);
    used_bytes += appendix_bytes;

    /* Read RQRMI model */
    model = lnmu_rqrmi64_init(NULL, NULL, 0);
    s >> size;
    buffer = new char[size];
    s.read(buffer, size);
    lnmu_rqrmi64_load(model, (void*)buffer, size);
    delete[] buffer;

    total_bytes += size;
    used_bytes += size;

    preader = bucket_reader(data, apdx, use_64bit);

    return 0;
}

void
db_reader::query(std::array<uint64_t, N> keys,
                 std::array<int, N> &num,
                 std::array<char*, N> &ptr)
{
    std::array<uint64_t, N> base_ranges;
    std::array<double, N> model_out;
    std::array<uint64_t, N> errors;
    std::array<int, N> search_results;
    std::array<int, N> val_results;

    lnmu_rqrmi64_inference_batch(model, &keys[0], &model_out[0], &errors[0]);
    /* Access to secondary search array (should fit the cache) */
    lnmu_range_array_search_batch(ranges, &keys[0], &model_out[0], &errors[0],
                                  &base_ranges[0], &search_results[0]);
    /* Access to validation array, 8*(compression-1) bytes per element */
    lnmu_range_array_validate_batch(ranges, &keys[0], &search_results[0],
                                    &base_ranges[0], &val_results[0]);
    preader.lookup_batch(keys, val_results, base_ranges, num, ptr);
    stats_counter++;
}

void
db_reader::query_perf(std::array<uint64_t, N> keys,
                      std::array<int, N> &num,
                      std::array<char*, N> &ptr)
{
    std::array<uint64_t, N> base_ranges;
    std::array<double, N> model_out;
    std::array<uint64_t, N> errors;
    std::array<int, N> search_results;
    std::array<int, N> val_results;

    PERF_START(inference);
    lnmu_rqrmi64_inference_batch(model, &keys[0], &model_out[0], &errors[0]);
    PERF_END(inference);

    /* Access to secondary search array (should fit the cache) */
    PERF_START(search);
    lnmu_range_array_search_batch(ranges, &keys[0], &model_out[0], &errors[0],
                                  &base_ranges[0], &search_results[0]);
    PERF_END(search);

    /* Access to validation array, 8*(compression-1) bytes per element */
    PERF_START(validate);
    lnmu_range_array_validate_batch(ranges, &keys[0], &search_results[0],
                                    &base_ranges[0], &val_results[0]);
    PERF_END(validate);

    PERF_START(lookup);
    preader.lookup_batch(keys, val_results, base_ranges, num, ptr);
    PERF_END(lookup);

    stats_inference += inference;
    stats_search += search;
    stats_validate += validate;
    stats_lookup += lookup;
    stats_counter++;
}

std::string
db_reader::debug(uint64_t key) const
{
    std::array<uint64_t, N> keys;
    std::array<uint64_t, N> base_ranges;
    std::array<double, N> model_out;
    std::array<uint64_t, N> errors;
    std::array<int, N> search_results;
    std::array<int, N> val_results;
    std::stringstream ss;
    uint16_t hash;

    keys[0] = key;
    lnmu_rqrmi64_inference_batch(model, &keys[0], &model_out[0], &errors[0]);
    lnmu_range_array_search_batch(ranges, &keys[0], &model_out[0], &errors[0],
                                  &base_ranges[0], &search_results[0]);
    lnmu_range_array_validate_batch(ranges, &keys[0], &search_results[0],
                                    &base_ranges[0], &val_results[0]);
    hash = hash_15bit_key(key, base_ranges[0]);
    ss << "Model search results:" << std::endl;
    ss << "key: " << key
       << " model-out: " << model_out[0]
       << " error: " << errors[0]
       << " base-range: " << base_ranges[0]
       << " bucket-index: " << val_results[0]
       << " hash: " << hash
       << std::endl;

    ss << "Page contents:" << std::endl;
    ss << preader.get_bucket_string(val_results[0], base_ranges[0]);
    ss << "Matched values:" << std::endl;
    ss << preader.get_key_values(val_results[0], base_ranges[0], key);

    return ss.str();
}
