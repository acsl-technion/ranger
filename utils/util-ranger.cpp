#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <list>
#include <zlib.h>

#include "lib/appendix.h"
#include "lib/binstream.h"
#include "lib/db-builder.h"
#include "lib/db-reader.h"
#include "lib/record.h"
#include "lib/record-file.h"

#include "lib/arguments.h"
#include "lib/perf.h"
#include "lib/print-utils.h"
#include "lib/random.h"

/* Application arguments */
static arguments args[] = {
/* Name    R  B  Default       Help */
{"file",   1, 0, NULL,         "Input filename."},
{"mode",   0, 0, "print-dump", "Operation mode (out of the following):"
                               "\n\n* 'print-records': treat 'input' "
                               "as a record-file. Print records to stdout "
                               "in human readable format (Note: use libranger "
                               "plugin for Minimap2 for generating "
                               "record-files)."
                               "\n\n"

                               "* 'build-db': treat 'input' as a "
                               "record-file. Create index db file. \n"
                               "Knobs: \n"
                               "-n1: ranges compression factor (default: 16)\n"
                               "-out: the output database filename."
                               "\n\n"

                               "* 'perf-test' treat 'input' "
                               "as an index db file. Perform 'n1' random "
                               "accesses to the index and print "
                               "performance statistics to stdout. \n"
                               "Knobs: \n"
                               "-n1: number of accesses (default: 1000000)"
                               "\n\n"

                               "* 'extract-ranges' treat 'input' "
                               "as an index db file. Print ranges "
                               "to stdout in human readable format."
                               "\n\n"
},
{"seed",   0, 0, "print",      "Random seed. Default is random seed."},
{"out",    0, 0, NULL,         "Output filename."},
{"factor", 0, 0, "0",          "Output file gzip compression factor "
                               "(in [0,9]). 0 Stands for no compression."},
{"n1",     0, 0, "0",          "General purpose numeric knob."},
{NULL,     0, 0, NULL,         "Various utils for inspecing libranger index "
                               "db files."},
};

static struct print_utils *print_utls;
static int seed = 0;
static int verbosity = 0;

static void
reset_seed()
{
    seed = ARG_INTEGER(args, "seed", 0);
    verbosity = ARG_INTEGER(args, "verbosity", 0);
    random_set_seed(seed);
}

static inline void
print_db_build_status(const db_builder &builder,
                      struct db_builder::status &status)
{
    if (!status.build_percent || status.build_percent % 5) {
        return;
    }

    print_utils_printf(print_utls,
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
    print_utils_flush(print_utls);
}

static inline void
print_model_errors(struct db_builder::status &status)
{
    print_utils_printf(print_utls, "Done training model. Erorr list: [");
    for (size_t i=0; i<status.model_error_num-1; ++i) {
        print_utils_printf(print_utls, "%d,", status.model_errors[i]);
    }
    print_utils_printf(print_utls, "%d]\n",
                       status.model_errors[status.model_error_num-1]);
}

static void
print_db_status(const db_builder &builder,
                struct db_builder::status status)
{
    if (status.status == db_builder::DB_BUILD) {
        print_db_build_status(builder, status);
    } else if (status.status == db_builder::DONE_TRAINING) {
        print_model_errors(status);
    }
    fflush(stdout);
}

static void
open_input_as_dumpfile(record_file &dmpfile)
{
    const char *filename;
    filename = ARG_STRING(args, "file", "");
    if (dmpfile.open_read(filename)) {
        printf("Cannot read input file \"%s\".\n", filename);
        exit(EXIT_FAILURE);
    }
}

static void
mode_print()
{
    record_file dmp;
    open_input_as_dumpfile(dmp);
    dmp.print(stdout);
}

static void
mode_build_db_from_dump()
{
    record_file dmpfile;
    db_builder db_builder(true);
    const char *out;
    int compression;
    int factor;
    gzFile fp;
    char mode[4];

    compression = ARG_INTEGER(args, "n1", 0);
    compression = compression != 0 ? compression : 16;

    out = ARG_STRING(args, "out", NULL);
    factor = ARG_INTEGER(args, "factor", 0);

    if (!out) {
        printf("Invalid configuration arguments\n");
        exit(EXIT_FAILURE);
    }

    open_input_as_dumpfile(dmpfile);

    printf("Building database...\n");
    fflush(stdout);
    PERF_START(build);
    db_builder.on_update().add_listener(print_db_status);
    db_builder.set_compression(compression);
    db_builder.build(dmpfile.get_size(), record_file::read_next, &dmpfile);
    PERF_END(build);
    printf("total time: %.3lf sec\n", build/1e9);

    printf("Training model... \n");
    db_builder.build_model();
    fflush(stdout);

    printf("Saving to '%s' (gzip compression factor: %d)...", out, factor);
    fflush(stdout);
    PERF_START(dump);
    snprintf(mode, sizeof(mode), "w%1dh", factor);
    fp = gzopen(out, mode);
    zlib_binstream base = zlib_binstream(fp, nullptr);
    binstream stream = binstream(base);
    db_builder.write(stream);
    gzclose(fp);
    PERF_END(dump);
    printf(" total time: %.3lf ms\n", dump/1e6);
}

static void
mode_extract_ranges()
{
    const char *filename, *out;
    db_reader db;
    size_t size;
    gzFile fp;
    FILE *fp2;

    filename = ARG_STRING(args, "file", "");
    out = ARG_STRING(args, "out", NULL);

    fp = gzopen(filename, "rb");
    zlib_binstream base = zlib_binstream(nullptr, fp);
    binstream stream = binstream(base);

    printf("Reading db file from '%s'...\n", filename);
    fflush(stdout);
    db.read(stream);
    gzclose(fp);

    printf("Writing ranges to file '%s'...\n", out);
    fp2 = fopen(out, "w");
    size = db.get_range_num();
    for (size_t i=0; i<size; ++i) {
        fprintf(fp2, "%lu\n", db.get_ranges()[i]);
    }
    fclose(fp2);
}

static void
mode_perf_test()
{
    std::array<uint64_t, db_reader::N> inputs;
    std::array<char*, db_reader::N> ptr;
    std::array<int, db_reader::N> num;
    uint64_t min, max, diff;
    const char *filename;
    int count;
    db_reader db;
    gzFile fp;


    filename = ARG_STRING(args, "file", "");
    count = ARG_INTEGER(args, "n1", 0);
    count = count != 0 ? count : 1000000;

    fp = gzopen(filename, "rb");
    zlib_binstream base = zlib_binstream(nullptr, fp);
    binstream stream = binstream(base);

    printf("Reading db file from '%s'...\n", filename);
    fflush(stdout);
    db.read(stream);
    gzclose(fp);

    printf("Performing test...\n");
    fflush(stdout);
    min = db.get_ranges()[0];
    max = db.get_ranges()[db.get_range_num()-1];
    diff = max-min;
    for (int i=0; i<count; i++) {
        for (int j=0; j<db_reader::N; ++j) {
            inputs[j] = min + (random_uint32() % diff);
        }
        db.query_perf(inputs, num, ptr);
    }

    printf("Stats: inference %.3lf ns search %.3lf ns "
           "validate %.3lf ns lookup %.3lf ns\n",
           db.get_stats_inference_ns(),
           db.get_stats_search_ns(),
           db.get_stats_validate_ns(),
           db.get_stats_lookup_ns());
}

int
main(int argc, char **argv)
{
    const char *mode;

    /* Parse arguments */
    arg_parse(argc, argv, args);

    reset_seed();
    printf("Running with seed %u\n", random_get_seed());

    print_utls = print_utils_init(stdout);
    mode = ARG_STRING(args, "mode", "print-records");

    if (!strcmp(mode, "print-records")) {
        mode_print();
    } else if (!strcmp(mode, "build-db")) {
        mode_build_db_from_dump();
    } else if (!strcmp(mode, "extract-ranges")) {
        mode_extract_ranges();
    } else if (!strcmp(mode, "perf-test")) {
        mode_perf_test();
    } else {
        printf("Mode '%s' is not supported.\n", mode);
        return EXIT_FAILURE;
    }

    print_utils_destroy(print_utls);
    return 0;
}

