#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include "binstream.h"
#include "db-builder.h"
#include "db-reader.h"
#include "libranger.h"
#include "record.h"

/*  Export method to shared library */
#define EXPORT extern "C" __attribute__((visibility("default")))

extern "C" {

struct record_extract_args {
    next_key_func_t func;
    void *args;
};

static inline void
logprint(struct libranger *idx, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (idx->logfile) {
        vfprintf(idx->logfile, fmt, args);
    }
    va_end (args);
}

static inline void
print_db_build_status(const db_builder &builder,
                      struct db_builder::status &status,
                      struct libranger *index)
{
    if (!status.build_percent || status.build_percent % 5) {
        return;
    }
    logprint(index,
             "%d%% (utilization: %.3lf%% ranges: %lu "
             "singletons: %.1lf %% "
             "unique-keys: %lu "
             "buckets-size: %.3lf MB "
             "appendix-size: %.3lf MB)\n",
             status.build_percent,
             builder.get_utilization()*100,
             builder.get_ranges().size(),
             builder.get_singleton_percent()*100,
             builder.get_disctinct_key_num(),
             builder.get_db_size()/1024.0/1024.0,
             builder.get_appendix().get_size()/1024.0/1024.0);
}

static inline void
print_model_errors(struct db_builder::status &status,
                   struct libranger *index)
{
    logprint(index, "Done training model. Erorr list: [");
    for (int i=0; i<(int)status.model_error_num-1; ++i) {
        logprint(index, "%d,", status.model_errors[i]);
    }
    logprint(index, "%d]\n", status.model_errors[status.model_error_num-1]);
}

static void
print_db_status(const db_builder &builder,
                struct db_builder::status status,
                void *args)
{
    struct libranger *index;
    index = (struct libranger*)args;
    if (!index->logfile) {
        return;
    } else if (status.status == db_builder::DB_BUILD) {
        print_db_build_status(builder, status, index);
    } else if (status.status == db_builder::START_TRAINING) {
        logprint(index, "Training RQ-RMI model... \n");
    } else if (status.status == db_builder::DONE_TRAINING) {
        print_model_errors(status, index);
    }
    fflush(index->logfile);
}

static int
get_next_record(struct record *m, void *args)
{
    struct record *record;
    struct record_extract_args *mea;
    int retval;

    mea = (struct record_extract_args*)args;
    record = (struct record *)m;
    retval = mea->func(&record->key, &record->value, mea->args);
    return retval;
}

EXPORT struct libranger *
libranger_init(FILE *logfile)
{
    struct libranger *index;
    index = new libranger();
    memset(index, 0, sizeof(*index));
    index->logfile = logfile;
    index->db_reader = (void*) new db_reader();
    return index;
}

EXPORT void
libranger_destroy(struct libranger *idx)
{
    delete (db_reader*)idx->db_reader;
    free(idx->raw_data);
    delete idx;
}

EXPORT void
libranger_build(struct libranger *idx,
                size_t key_num,
                bool use_64bit,
                int ratio,
                next_key_func_t next_record_func,
                void *next_record_func_args)
{
    db_reader *dbr = (db_reader *)idx->db_reader;
    struct record_extract_args mea;
    db_builder db_builder(use_64bit);
    mem_binstream memstream;
    binstream s(memstream);

    mea.func = next_record_func;
    mea.args = next_record_func_args;

    idx->use_64bit = use_64bit;

    db_builder.on_update().add_listener(print_db_status, idx);
    db_builder.set_compression(ratio);
    db_builder.build(key_num, get_next_record, &mea);

    db_builder.build_model();

    logprint(idx, "Writing index as binary data...\n");
    db_builder.write(s);
    dbr->read(s);
    idx->raw_data = memstream.detach_data(&idx->size);
};

EXPORT void
libranger_save(struct libranger *idx, FILE *fp)
{
    fwrite((void*)&idx->size, sizeof(size_t), 1, fp);
    fwrite(idx->raw_data, sizeof(char), idx->size, fp);
}

EXPORT struct libranger *
libranger_load(FILE *fp)
{
    struct libranger *index;
    char *data;
    db_reader *dbr;

    index = new libranger;
    dbr = new db_reader;

    assert(fread(&index->size, sizeof(size_t), 1, fp) == 1);

    data = new char[index->size];
    assert(fread(data, sizeof(char), index->size, fp) == index->size);

    mem_binstream memstream(data, index->size);
    binstream s(memstream);

    dbr->read(s);
    index->db_reader = (void*)dbr;
    index->raw_data = nullptr;

    /* No need for raw data, "dbr" contains a copy */
    delete[] data;
    return index;
}

EXPORT void
libranger_get_stats(struct libranger *idx)
{
    db_reader *dbr = (db_reader *)idx->db_reader;
    idx->total_bytes = dbr->get_total_bytes();
    idx->appendix_bytes = dbr->get_appendix_bytes();
    idx->redundant_bytes = dbr->get_redundant_bytes();
    idx->distinct_key_num = dbr->get_distinct_key_num();
    idx->singleton_num = dbr->get_singleton_num();
    idx->total_key_num = dbr->get_total_key_num();
    idx->used_bytes = dbr->get_used_bytes();
    idx->prefix_bits_mean = dbr->get_prefix_bits_mean();
    idx->prefix_bits_stddev = dbr->get_prefix_bits_stddev();
}

EXPORT void
libranger_extrat_ranges(struct libranger *idx,
                             const uint64_t **out,
                             size_t *size)
{
    db_reader *dbr = (db_reader *)idx->db_reader;
    *out = dbr->get_ranges();
    *size = dbr->get_range_num();
}

EXPORT uint32_t*
libranger_get_occ_list(struct libranger *idx, size_t *count)
{
    db_reader *dbr = (db_reader *)idx->db_reader;
    std::vector<uint32_t> vec = dbr->get_occurence_list();
    uint32_t *out = (uint32_t*)malloc(sizeof(uint32_t)*vec.size());
    memcpy(out, &vec[0], vec.size()*sizeof(uint32_t));
    *count = vec.size();
    return out;
}

EXPORT uint64_t
libranger_get_appendix_size(struct libranger *idx)
{
    return idx ? idx->appendix_bytes : -1;
}

EXPORT void
libranger_query(struct libranger *idx,
                     uint64_t *keys,
                     int *num,
                     char **ptr)
{
    std::array<uint64_t, db_reader::N> *key_arr;
    std::array<int, db_reader::N> *num_arr;
    std::array<char*, db_reader::N> *ptr_arr;
    db_reader *dbr = (db_reader *)idx->db_reader;
    key_arr = reinterpret_cast<decltype(key_arr)>(keys);
    num_arr = reinterpret_cast<decltype(num_arr)>(num);
    ptr_arr = reinterpret_cast<decltype(ptr_arr)>(ptr);
    dbr->query(*key_arr, *num_arr, *ptr_arr);
}

EXPORT void
libranger_query_perf(struct libranger *idx,
                          uint64_t *keys,
                          int *num,
                          char **ptr)
{
    std::array<uint64_t, db_reader::N> *key_arr;
    std::array<int, db_reader::N> *num_arr;
    std::array<char*, db_reader::N> *ptr_arr;
    db_reader *dbr = (db_reader *)idx->db_reader;
    key_arr = reinterpret_cast<decltype(key_arr)>(keys);
    num_arr = reinterpret_cast<decltype(num_arr)>(num);
    ptr_arr = reinterpret_cast<decltype(ptr_arr)>(ptr);
    dbr->query_perf(*key_arr, *num_arr, *ptr_arr);
}

EXPORT char*
libranger_get_perf_string(struct libranger *idx)
{
    db_reader *dbr = (db_reader *)idx->db_reader;
    const char *msg = "inference %.3lf ns search %.3lf ns "
                      "validate %.3lf ns lookup %.3lf ns \n";
    char *out = NULL;
    size_t size = snprintf(out, 0, msg,
                           dbr->get_stats_inference_ns(),
                           dbr->get_stats_search_ns(),
                           dbr->get_stats_validate_ns(),
                           dbr->get_stats_lookup_ns());
    out = (char*)malloc(sizeof(char)*size);
    snprintf(out, size, msg,
             dbr->get_stats_inference_ns(),
             dbr->get_stats_search_ns(),
             dbr->get_stats_validate_ns(),
             dbr->get_stats_lookup_ns());
    return out;
}


} /* extern "C" */
