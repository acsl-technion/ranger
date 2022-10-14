#include <cstring>
#include <cstdlib>

#include "appendix.h"

static int
compare_uint64(const void *a, const void *b)
{
    return *(uint64_t*)a >= *(uint64_t*)b;
}

uint64_t
appendix::add_element64(std::vector<uint64_t> &vals)
{
    uint64_t out;

    /* Sort elements in "vals" */
    qsort(&vals[0], vals.size(), sizeof(uint64_t), compare_uint64);

    out = ((uint64_t)data.size() << 32) | (uint32_t)vals.size();
    for (uint64_t e : vals) {
        push(e);
    }
    return out;
}

uint32_t
appendix::add_element32(std::vector<uint32_t> &vals)
{
    uint32_t out;
    uint32_t size;

    size = vals.size();
    push(size);

    /* Sort elements in "vals" */
    qsort(&vals[0], vals.size(), sizeof(uint32_t), compare_uint64);

    out = (uint32_t)data.size();
    for (uint64_t e : vals) {
        push(e);
    }
    return out;
}

uint64_t
appendix::get_size() const
{
    return data.size();
}

const char *
appendix::get_data() const
{
    return &data[0];
}
