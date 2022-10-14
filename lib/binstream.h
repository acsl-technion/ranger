#ifndef BINSTREAM_H
#define BINSTREAM_H

#include <cstring>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <zlib.h>

/* Used for late-binding of read/write operations. Basic implementation
 * supports std::iostream integration */
class base_binstream {
    std::ostream *ostream;
    std::istream *istream;
public:

    base_binstream()
        : ostream(nullptr),
          istream(nullptr)
    {}

    base_binstream(std::ostream *os, std::istream *is)
        : ostream(os),
          istream(is)
    {}

    virtual
    ~base_binstream()
    {}

    virtual void
    write(const void *data, size_t size)
    {
        ostream->write((char*)data, size);
    }

    virtual void
    read(void *data, size_t size)
    {
        istream->read((char*)data, size);
    }

    virtual base_binstream&
    clone() const
    {
        base_binstream *b = new base_binstream(*this);
        return *b;
    }
};

/* Write data to memory */
class mem_binstream : public base_binstream {

    struct memdata {
        char *ptr;
        size_t size;
        size_t reserved;
        size_t cursor;
        size_t refcount;
        int allocated;
    };
    struct memdata *data;

public:

    mem_binstream()
    {
        data = new memdata();
        data->size = 0;
        data->refcount  =1;
        data->cursor = 0;
        data->reserved = 32;
        data->ptr = (char*)malloc(data->reserved);
        data->allocated = 1;
    }

    mem_binstream(char *ptr, size_t size)
    {
        data = new memdata();
        data->size = size;
        data->refcount =1;
        data->cursor = 0;
        data->reserved = size;
        data->ptr = ptr;
        data->allocated = 0;
    }

    mem_binstream(const mem_binstream& other)
    : base_binstream(),
      data(other.data)
    {
        data->refcount++;
    }

    virtual
    ~mem_binstream()
    {
        data->refcount--;
        if (!data->refcount) {
            if (data->allocated) {
                free((void*)data->ptr);
            }
            delete data;
        }
    }

    virtual void
    write(const void *data, size_t size)
    {
        void *dst;
        if (this->data->size + size > this->data->reserved) {
            while (this->data->size + size > this->data->reserved) {
                this->data->reserved <<=1;
            }
            this->data->ptr =
                (char*)realloc((void*)this->data->ptr, this->data->reserved);
        }
        dst = (void*)(this->data->ptr + this->data->size);
        memcpy(dst, data, size);
        this->data->size += size;
    }

    virtual void
    read(void *data, size_t size)
    {
        void *src = (void*)(this->data->ptr + this->data->cursor);
        memcpy(data, src, size);
        this->data->cursor += size;
    }

    /* Detached data from all copies of this */
    void*
    detach_data(size_t *out_size)
    {
        void *out = (void*)data->ptr;
        out = realloc(out, data->size);
        *out_size = data->size;
        data->size = 0;
        data->reserved = 32;
        data->cursor = 0;
        data->ptr = (char*)malloc(data->reserved);
        data->allocated = 1;
        return out;
    }

    virtual base_binstream&
    clone() const
    {
        mem_binstream *b = new mem_binstream(*this);
        return *b;
    }
};

class binstream {

    constexpr static int header_length = 16;
    base_binstream *base;

public:

    binstream()
        : base(new base_binstream())
    {}

    binstream(base_binstream &base)
        : base(&base.clone())
    {}

    binstream(std::ostream &os, std::istream &is)
        : base(new base_binstream(&os, &is))
    {}

    binstream(std::ostream &os)
        : base(new base_binstream(&os, nullptr))
    {}

    binstream(std::istream &is)
        : base(new base_binstream(nullptr, &is))
    {}

    virtual ~binstream()
    {
        delete base;
    }

    int
    read_header(const char *header_name)
    {
        char header[header_length];
        uint16_t endianness;
        uint16_t version;

        memset(header, 0, sizeof(header));
        base->read(header, header_length);

        if (strncmp(header, header_name, header_length)) {
            throw std::runtime_error("Cannot read: invalid header");
        }

        base->read(&endianness, sizeof(endianness));
        if (endianness != 1) {
            throw std::runtime_error("Cannot read: wrong endianess");
        }

        base->read(&version, sizeof(version));
        return version;
    }

    void
    write_header(const char *header_name,
                 uint16_t version)
    {
        char header[header_length];
        int len;
        uint16_t data;

        len = strlen(header_name);
        len = len > header_length ? header_length : len;
        memset(header, 0, sizeof(header));
        memcpy(header, header_name, len);

        /* Common header to all version */
        base->write(header, sizeof(header));
        data=1;
        base->write(&data, sizeof(data));
        data=version;
        base->write(&data, sizeof(data));
    }

    template<typename T>
    binstream&
    write(const T &data)
    {
        base->write(&data, sizeof(T));
        return *this;
    }

    binstream&
    write(const void *data, size_t size)
    {
        base->write(data, size);
        return *this;
    }

    template<typename T>
    binstream&
    read(T &data)
    {
        base->read(&data, sizeof(T));
        return *this;
    }

    binstream&
    read(void *data, size_t size)
    {
        base->read(data, size);
        return *this;
    }

    template<typename T>
    binstream&
    operator<<(const std::vector<T> &vec)
    {
        size_t val = vec.size();
        *this << val;
        for (size_t i=0; i<vec.size(); i++) {
            *this << vec.at(i);
        }
        return *this;
    }

    template<typename T>
    binstream&
    operator>>(std::vector<T> &vec)
    {
        size_t size;
        *this >> size;
        vec.resize(size);
        for (size_t i=0; i<vec.size(); i++) {
            *this >> vec.at(i);
        }
        return *this;
    }

    template<typename T>
    binstream&
    operator<<(const T &data)
    {
        base->write(&data, sizeof(T));
        return *this;
    }

    template<typename T>
    binstream&
    operator>>(T &data)
    {
        base->read(&data, sizeof(T));
        return *this;
    }
};

class zlib_binstream : public base_binstream {
    gzFile ostream;
    gzFile istream;

public:

    zlib_binstream()
        : ostream(nullptr),
          istream(nullptr)
    {}

    zlib_binstream(gzFile os, gzFile is)
        : ostream(os),
          istream(is)
    {}

    void
    write(const void *data, size_t size)
    {
        gzwrite(ostream, (char*)data, size);
    }

    void
    read(void *data, size_t size)
    {
        gzread(istream, (char*)data, size);
    }

    base_binstream&
    clone() const
    {
        zlib_binstream *b = new zlib_binstream(*this);
        return *b;
    }
};

#endif
