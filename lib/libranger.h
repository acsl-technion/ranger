#ifndef LIBRANGER_H
#define LIBRANGER_H

#ifdef __cplusplus
# include <cstdint>
# include <cstdio>
#else
# include <stdint.h>
# include <stdio.h>
# include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Query batch size */
#define BATCH_SIZE 4

struct libranger {
    size_t size;
    bool use_64bit;
    void *raw_data;
    void *db_reader;
    FILE *logfile;
    /* Stats */
    size_t total_bytes;
    size_t appendix_bytes;
    size_t redundant_bytes;
    size_t distinct_key_num;
    size_t used_bytes;
    size_t singleton_num;
    size_t total_key_num;
    double prefix_bits_mean;
    double prefix_bits_stddev;
};

/**
 * @brief A function pointer for a user defined function for reading records.
 * Upon any invocation of this, "*key" and "*value" should be set with the
 * new record data. This data type is used by "libranger_build" method.
 * @param[out] key Set by this with the new record's key
 * @param[out] value Set by this with the new record's value
 * @param[in] args The user defined argument that is passed to "libranger_build"
 * @returns 0 As long as there are more records.
 */
typedef int(*next_key_func_t)(uint64_t *key, uint64_t *value, void *args);

/**
 * @brief Initiate a new Ranger data structure.
 * @param logfile Print logs to this. May be NULL.
 */
struct libranger * libranger_init(FILE *logfile);

/**
 * @brief Free allocated memory for "idx"
 */
void libranger_destroy(struct libranger *idx);

/**
 * @brief Build a new Ranger database.
 * @param idx An initiated Ranger data structure.
 * @param size How many records are going to be indexed
 * @param use_64bit Use 64-bit values (or 32-bit values)
 * @param ratio NuevoMatchUp compression ratio. Recommended value: 16
 * @param next_record_func A function pointer for reading the records.
 * @param next_record_func_args User defined argument for "next_record_func"
 */
void libranger_build(struct libranger *idx,
                     size_t size,
                     bool use_64bit,
                     int ratio,
                     next_key_func_t next_record_func,
                     void *next_record_func_args);

/** @brief Save/load the data-sturcute "idx" from the current cursor of file
 *  "fp". Updates "fp" cursor to point just after the data-structure. */
void libranger_save(struct libranger *idx, FILE *fp);
struct libranger * libranger_load(FILE *fp);

/** @brief Allocates "*out" to have "*size" elements (both set by this), each
 *  element is a range used by "idx". "*out" is sorted. */
void libranger_extrat_ranges(struct libranger *idx,
                             const uint64_t **out,
                             size_t *size);

/** @brief Populates "idx" with statistic information */
void libranger_get_stats(struct libranger *idx);

/** @brief Returns a sorted list of the value count for each key in "idx" */
uint32_t* libranger_get_occ_list(struct libranger *idx, size_t *count);

/** @brief Allocates a string with various performance statistics. Should be
 *  freed by the user. */
char* libranger_get_perf_string(struct libranger *idx);

/* Returns the size of the position list of "idx" in bytes, or -1 on error */
uint64_t libranger_get_appendix_size(struct libranger *idx);

/** @brief Performs query on BATCH_SIZE "keys". Sets each element in "num" to
 *  hold the number of matched keys, and each element in "ptr" to hold pointers
 *  to the matched values. "libranger_query_perf" also saves performance
 *  statistics, thus is a bit slower. */
void libranger_query(struct libranger *idx,
                     uint64_t *keys,
                     int *num,
                     char **ptr);
void libranger_query_perf(struct libranger *idx,
                          uint64_t *keys,
                          int *num,
                          char **ptr);

#ifdef __cplusplus
};
#endif

#endif
