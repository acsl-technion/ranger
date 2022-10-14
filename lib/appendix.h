#ifndef APPENDIX_H
#define APPENDIX_H

#include <map>
#include <vector>

class appendix {

    std::vector<char> data;

    template <typename T>
    void push(const T &elem)
    {
        const char *ptr = (const char*)&elem;
        for (size_t i=0; i<sizeof(T); ++i) {
            this->data.push_back(ptr[i]);
        }
    }

public:

    appendix() = default;
    appendix(const appendix&) = delete;

    /* Adds "vals" into the appendix. Returns value to save in bucket. */
    uint64_t add_element64(std::vector<uint64_t> &vals);
    uint32_t add_element32(std::vector<uint32_t> &vals);

    /* Returns the size of this, in bytes */
    uint64_t get_size() const;

    const char *get_data() const;
};


#endif
