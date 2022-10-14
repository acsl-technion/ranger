#ifndef BUCKET_BUILDER_H
#define BUCKET_BUILDER_H

#include <array>
#include <vector>
#include <map>

#include "appendix.h"
#include "record.h"

class bucket_builder {

    struct attr {
        int count;
        uint16_t hash; /* LSbit is 1 iff saved_val is apdx pointer */
        uint64_t saved_val64;
        uint32_t saved_val32;
        std::vector<uint64_t> values64;
        std::vector<uint32_t> values32;
        attr();
    };

    bool use_64bit;
    uint64_t smallest_key;
    std::map<uint64_t, struct attr*> keys;

    struct sortargs {
        std::map<uint64_t, struct attr*> *keys;
        bool use_64bit;
    };

public:

    bucket_builder(bool use_64bit);
    bucket_builder(const bucket_builder &other) = delete;
    ~bucket_builder();

    /* Pushes a record "m" into this, returns 0 if it can be inserted
     * into the bucket w/o breaking the collision constraint. */
    int push(struct record *m);

    /* Clear all records from this */
    void clear();

    /* Populate the bucket at "ptr". The bucket must have at least
     * "get_bucket_size" bytes allocated. */
    void pack(char *ptr);

    /* Populates the appendix with a new appendix bucket */
    void populate_appendix(appendix &a);

    /* Returns how many bytes are used by this */
    size_t get_used_bytes() const;

    /* Returns the smallest key in this */
    uint64_t get_smallest_key() const;

    /* Returns the common prefix of this */
    uint8_t get_common_prefix_bits() const;

    /* Returns the number of distinct keys in this */
    size_t get_distinct_key_num() const;

    /* Returns the total number of keys in this */
    size_t get_total_key_num() const;

    /* Return number of singletons */
    size_t get_singleton_num() const;

    /* Returns true iff this uses 64bit values */
    bool get_use_64bit() const;

    /* Returns the list of values accosiated with a key */
    const std::vector<uint64_t>* get_key_values64(uint64_t key) const;
    const std::vector<uint32_t>* get_key_values32(uint64_t key) const;

    /* Returns the number of bytes in the bucket */
    static size_t get_size_bytes(bool use_64bit);

private:

    /* Returns the attributes of the given key. Initializes attributes for
     * new keys. Returns NULL if this has reached maximum capacity */
    attr * get_key_attr(uint64_t key);

    /* Populate the record within this. Returns 0 on success */
    int populate_record(struct record *m, struct attr *key_attr);

    /* Try insert "m" into this. Returns 0 on success. */
    int try_insert(struct record *m);

    /* Returns the key order in bucket */
    std::vector<uint64_t> get_key_order();

    static int key_sort(const void *pa, const void *pb, void *args);
};



#endif
