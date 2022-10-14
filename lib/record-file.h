#ifndef RECORD_FILE_H
#define RECORD_FILE_H

#include <cstdio>
#include <map>
#include <vector>
#include <zlib.h>
#include "record.h"

/* Utilities for key dumpfiles */
class record_file {
public:
    using map_values = std::vector<uint64_t>;
    enum modes { MODE_NONE, MODE_READ, MODE_WRITE };

private:
    std::map<uint64_t, map_values*> map;
    size_t size;
    gzFile file;
    char mode;

public:

    record_file();
    record_file(const record_file&) = delete;
    ~record_file();

    /* Opens "filename" for reading. Returns 0 in success. */
    int open_read(const char *filename);

    /* Adds record to this. Returns 1 iff key is unique */
    int add_record(struct record &m);

    /* Returns the mode of this */
    enum modes get_mode() const;

    /* Opens "filename" for writing with compression "c".
     * Returns 0 on success */
    int open_write(const char *filename, int c = 0);

    void close();

    /* Writes records into file. Returns 0 on success */
    int write_records();

    /* Reads records from file */
    void read_records();

    /* Reads file and writes to "file" in human redable format */
    void print(FILE *file);

    const std::map<uint64_t, map_values*>& get_map() const;

    /* Used as "next_record_func_t" with this */
    static int read_next(struct record *m, void *args);

    /* Returns the size of this */
    size_t get_size() const;
};




#endif
