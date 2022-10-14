#include <cstdlib>
#include "record.h"
#include "record-file.h"

static int
compare_uint64(const void *a, const void *b)
{
    return *(uint64_t*)a >= *(uint64_t*)b;
}


record_file::record_file()
: size(0),
  file(nullptr),
  mode(0)
{}

record_file::~record_file()
{
    gzclose(file);
    for (auto &it : map) {
        delete it.second;
    }
}

int
record_file::open_read(const char *filename)
{
    file = gzopen(filename, "rb");
    mode = 'r';
    gzread(file, &size, sizeof(size));
    return !file;
}

int
record_file::open_write(const char *filename, int c)
{
    char mode[4];
    snprintf(mode, sizeof(mode), "w%1dh", c);
    this->mode = 'w';
    file = gzopen(filename, mode);
    return !file;
}

void
record_file::close()
{
    gzclose(file);
    file = nullptr;
    mode = 0;
}

int
record_file::add_record(struct record &m)
{
    int retval = 0;
    if (map.find(m.key) == map.end()) {
        map[m.key] = new map_values();
        retval = 1;
    }
    map[m.key]->push_back(m.value);
    size++;
    return retval;
}

record_file::modes
record_file::get_mode() const
{
    return !file ? MODE_NONE :
           mode == 'r' ? MODE_READ :
           MODE_WRITE;
}

size_t
record_file::get_size() const
{
    if (mode != 'r') {
        return 0;
    }
    return size;
}

int
record_file::write_records()
{
    uint64_t key;
    map_values *values;

    if (!file || !mode || mode == 'r') {
        return 1;
    }
    gzwrite(file, &size, sizeof(size));
    for (auto &record_it : map) {
        /* Sort the values */
        key = record_it.first;
        values = record_it.second;
        qsort(&(*values)[0], values->size(), sizeof(uint64_t), compare_uint64);
        /* Store dump */
        for (uint64_t &value_it : *values) {
            gzwrite(file, &key, sizeof(key));
            gzwrite(file, &value_it, sizeof(value_it));
        }
    }
    return 0;
}

void
record_file::read_records()
{
    uint64_t last_key;
    map_values *mp;
    struct record m;

    last_key = -1;
    for (size_t i=0; i<size; i++) {
        record_read_from_file(&m, file);
        if (m.key != last_key) {
            map[m.key] = new map_values();
            mp = map[m.key];
        }
        last_key = m.key;
        mp->push_back(m.value);
    }
}

const std::map<uint64_t, record_file::map_values*>&
record_file::get_map() const
{
    return map;
}

void
record_file::print(FILE *os)
{
    struct record record;
    fprintf(os, "Total %lu records\n", size);

    while (1) {
        if (record_read_from_file(&record, file)) {
            break;
        }
        fprintf(os, "%14lu %14lu\n", record.key, record.value);
    }
}

int
record_file::read_next(struct record *m, void *args)
{
    record_file *me = (record_file *)args;
    return record_read_from_file(m, me->file);
}
