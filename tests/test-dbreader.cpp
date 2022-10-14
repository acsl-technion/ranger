#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <set>
#include <zlib.h>

#include "lib/binstream.h"
#include "lib/db-builder.h"
#include "lib/db-reader.h"
#include "lib/record-file.h"
#include "lib/record.h"
#include "lib/arguments.h"
#include "lib/random.h"
#include "lib/util.h"

/* Application arguments */
static arguments args[] = {
/* Name                 R  B  Def       Help */
{"dump-file",           0, 0, "",       "Kmer dump file."},
{"db-file",             0, 0, "",       "Database file."},
{"override",            0, 1, 0,        "Override databsae file."},
{"keep",                0, 1, 0,        "Keep generated files."},
{"export-ranges",       0, 0, NULL,     "Export db ranges to file."},
{"seed",                0, 0, "print",  "Empty or 0 for random seed."},
{"verbosity",           0, 0, "0",      "Test verbosity."},
{NULL,                  0, 0, NULL,     "Tests the correctness of db-reader."},
};

static struct {
    int randomize;
    const char *dumpfile;
    const char *dbfile;
    uint64_t key_mask;
    uint32_t key_size;
    int key_num;
    int compression;
} config;

static record_file kdump;
static int verbosity;
static size_t unique_keys = 0;

static void
print_db_status(const db_builder &builder, struct db_builder::status status)
{
    if (!status.build_percent || status.build_percent % 5) {
        return;
    }

    switch(status.status) {
    case db_builder::DB_BUILD:
        printf("%d%% (utilization: %.3lf%% ranges: %lu "
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

        /* Make sure the randomized key num equals to the nubmer of unique
         * keys */
        if (unique_keys && status.build_percent == 100) {
            if (unique_keys != builder.get_disctinct_key_num()) {
                printf("Kmer num mismatch \n");
                exit(EXIT_FAILURE);
            }
        }

        break;
    case db_builder::DONE_TRAINING:
        printf("Done training model. Erorr list: [");
            for (size_t i=0; i<status.model_error_num-1; ++i) {
                printf("%d,", status.model_errors[i]);
            }
            printf("%d]\n", status.model_errors[status.model_error_num-1]);
        break;
    default:
        break;
    }

    fflush(stdout);
}

static void
reset_seed()
{
    int seed;
    seed = ARG_INTEGER(args, "seed", 0);
    verbosity = ARG_INTEGER(args, "verbosity", 0);
    random_set_seed(seed);
}

static void
test_init(int argc, char **argv)
{
    arg_parse(argc, argv, args);

    reset_seed();
    printf("Running with seed %u\n", random_get_seed());

    memset(&config, 0, sizeof(config));
    config.dbfile = ARG_STRING(args, "db-file", "");
    config.dumpfile = ARG_STRING(args, "dump-file", "");
    config.randomize = !strlen(config.dumpfile);

    if (config.randomize) {
        printf("Works with random records\n");
    } else {
        printf("Works with a custom dump-file '%s'\n", config.dumpfile);
    }

    if (!strlen(config.dbfile)) {
        config.dbfile = "tmp.db";
    }
    if (!strlen(config.dumpfile)) {
        config.dumpfile = "tmp.dump";
    }

    /* Randomize test configuration */
    config.key_size = 15 + (random_uint32() % 3);
    config.key_mask = (1ULL<<(config.key_size*2)) - 1;
    config.key_num = 1<<(20 +(random_uint32() % 5));
    config.compression = 1<<(random_uint32()&3);

    printf("Test configuration: "
           "key-size: %u key-mask: 0x%lX "
           "compression: %d "
           "key-num: %d \n",
           config.key_size,
           config.key_mask,
           config.compression,
           config.key_num);

    fflush(stdout);
}

static int
randomize_next_record(struct record *m, void *args)
{
    static int remaining_key = 0;
    static int counter = 0;
    static uint64_t last_key;
    static double p = 0;

    if (counter >= config.key_num) {
        return 1;
    }

    counter++;
    m->value = random_uint64();

    /* Generate the same key as before */
    if (remaining_key) {
        remaining_key--;
        m->key = last_key;
        goto exit;
    }

    /* Set key and value (keys must be sorted) */
    p += random_double()/config.key_num;
    last_key = p * config.key_mask;
    m->key = last_key;

    /* Singleton */
    if (random_coin(0.9)) {
        remaining_key = 0;
    } else {
        remaining_key = 1 + (random_uint32() & 63);
    }

exit:
    unique_keys += kdump.add_record(*m);
    return 0;
}

static bool
override_file(const char *filename)
{
    FILE *fp2;
    if (ARG_BOOL(args, "override", 0)) {
        return true;
    }

    /* Skip if file already exists */
    fp2 = fopen(filename, "r");
    if (fp2) {
        fclose(fp2);
        printf("File \"%s\" already exists\n", filename);
        return false;
    }
    return true;
}

static void
populate_records(db_builder &db_builder)
{
    const char *ranges_fname;
    FILE *fp;

    if (!override_file(config.dumpfile)) {
        printf("Kmer dump file \"%s\" exists, building "
               "database from it\n", config.dumpfile);
        kdump.open_read(config.dumpfile);
        db_builder.build(kdump.get_size(), record_file::read_next, &kdump);
    } else {
        kdump.open_write(config.dumpfile, 0);
        db_builder.build(config.key_num, randomize_next_record, NULL);
    }

    /* Export ranges */
    ranges_fname = ARG_STRING(args, "export-ranges", NULL);
    if (ranges_fname) {
        printf("Saving DB ranges as text to \"%s\"...\n", ranges_fname);
        fp = fopen(ranges_fname, "w");
        for (auto &r : db_builder.get_ranges()) {
            fprintf(fp, "%lu\n", r);
        }
        fclose(fp);
    }

    printf("Building model (total ranges: %lu)...\n",
           db_builder.get_range_num());
    fflush(stdout);
    db_builder.build_model();
}

static void
generate_database()
{
    db_builder db_builder(true);
    gzFile fp;

    if (!override_file(config.dbfile)) {
        return;
    }

    printf("Generating database... \n");
    fflush(stdout);
    db_builder.on_update().add_listener(print_db_status);
    db_builder.set_compression(config.compression);
    populate_records(db_builder);

    printf("Saving db file to '%s'...\n", config.dbfile);
    fflush(stdout);

    fp = gzopen(config.dbfile, "w0h");
    zlib_binstream base = zlib_binstream(fp, nullptr);
    binstream stream = binstream(base);

    db_builder.write(stream);
    gzclose(fp);

    if (kdump.get_mode() == record_file::MODE_WRITE) {
        printf("Saving dump file to '%s'...\n", config.dumpfile);
        fflush(stdout);
        kdump.write_records();
    } else {
        /* kdump was opened for read in streaming mode; close it so we can
         * later read its data in vector mode */
        kdump.close();
    }
}

static void
test_exact_match(std::vector<uint64_t> &keys, db_reader &db)
{
    std::array<record_file::map_values*, db_reader::N> values_arr;
    std::array<uint64_t, db_reader::N> key_arr;
    std::array<int, db_reader::N> num;
    std::array<char*, db_reader::N> ptrs;
    uint64_t v;
    int idx;

    auto &map = kdump.get_map();

    for (int i=0; i<db_reader::N; i++) {
        idx = random_uint32() % keys.size();
        key_arr[i] = keys[idx];
        values_arr[i] = map.at(key_arr[i]);
    }

    db.query_perf(key_arr, num, ptrs);

    for (int i=0; i<db_reader::N; i++) {
        /* Check that the number of values matches */
        if (num[i] != (int)values_arr[i]->size()) {
            printf("\nError: value count mismatch for key %lu: "
                   "got %d expected %lu (batch idx: %d)\n",
                   key_arr[i],
                   num[i],
                   map.at(key_arr[i])->size(),
                   i);
            printf("Debug string: %s\n", db.debug(key_arr[i]).c_str());
            exit(EXIT_FAILURE);
        }
        /* Check that value is found */
        for (int j=0; j<num[i]; j++) {
            v = *((uint64_t*)ptrs[i] + j);
            if (v != values_arr[i]->at(j)) {
                printf("\nError: value mismatch for key %lu: "
                       "expected %lu, got %lu (batch idx: %d)\n",
                       key_arr[i],
                       values_arr[i]->at(j),
                       v,
                       i);
                printf("%s\n", db.debug(key_arr[i]).c_str());
                exit(EXIT_FAILURE);
            }
        }
    }
}

static void
perform_check(std::vector<uint64_t> &keys, db_reader &db)
{
    const int TEST_NUM = 1e5;

    reset_seed();
    printf("Performing test");
    fflush(stdout);

    for (int i =0; i<TEST_NUM; ++i) {
        test_exact_match(keys, db);
        if (!(i %(TEST_NUM/10))) {
            printf(".");
            fflush(stdout);
        }
    }

    printf(" Done\n"
           "Stats: inference %.3lf ns search %.3lf ns "
           "validate %.3lf ns lookup %.3lf ns\n",
           db.get_stats_inference_ns(),
           db.get_stats_search_ns(),
           db.get_stats_validate_ns(),
           db.get_stats_lookup_ns());

}

static void
read_database(std::vector<uint64_t> &keys, db_reader &db)
{
    gzFile fp;

    fp = gzopen(config.dbfile, "rb");

    zlib_binstream base = zlib_binstream(nullptr, fp);
    binstream stream = binstream(base);

    printf("Reading db file from '%s'...\n", config.dbfile);
    fflush(stdout);
    db.read(stream);

    if (!kdump.get_mode()) {
        printf("Reading key dump file from '%s'...\n", config.dumpfile);
        kdump.open_read(config.dumpfile);
        kdump.read_records();
    }
}

static void
generate_key_list(std::vector<uint64_t> &keys)
{
    auto &map = kdump.get_map();
    keys.reserve(map.size());
    for (auto &it : map) {
        keys.push_back(it.first);
    }
}

int
main(int argc, char **argv)
{
    std::vector<uint64_t> keys;
    db_reader db;

    test_init(argc, argv);

    generate_database();
    read_database(keys, db);
    generate_key_list(keys);
    perform_check(keys, db);

    if (!ARG_BOOL(args, "keep", 0) && config.randomize) {
        printf("Deleting \"%s\" and \"%s\"\n", config.dbfile, config.dumpfile);
        remove(config.dbfile);
        remove(config.dumpfile);
    }
}
