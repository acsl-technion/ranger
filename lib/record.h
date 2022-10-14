#ifndef RECORD_H
#define RECORD_H

#include <cstdio>
#include <cstdint>
#include <zlib.h>

/* Always 128bit */
struct record {
    uint64_t key;
    uint64_t value;
};

/* Populates "m" with the next record. Returns 0 on success */
typedef int(*next_record_func_t)(struct record *m, void *args);

/* Read a single record from dumpfile */
static inline int
record_read_from_file(struct record *record, void *file)
{
    gzFile fp = (gzFile)file;
    return (gzread(fp, record, sizeof(*record)) <= 0) ? EOF : 0;
}

#endif
