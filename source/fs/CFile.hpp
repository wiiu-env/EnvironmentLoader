#ifndef CFILE_HPP_
#define CFILE_HPP_

#include <stdio.h>
#include <string>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <wut_types.h>

class CFile {
public:
    enum eOpenTypes {
        ReadOnly,
        WriteOnly,
        ReadWrite,
        Append
    };

    CFile();

    CFile(const std::string &filepath, eOpenTypes mode);

    CFile(const uint8_t *memory, int32_t memsize);

    virtual ~CFile();

    int32_t open(const std::string &filepath, eOpenTypes mode);

    int32_t open(const uint8_t *memory, int32_t memsize);

    BOOL isOpen() const {
        if (iFd >= 0)
            return true;

        if (mem_file)
            return true;

        return false;
    }

    void close();

    int32_t read(uint8_t *ptr, size_t size);

    int32_t write(const uint8_t *ptr, size_t size);

    int32_t fwrite(const char *format, ...);

    int32_t seek(long int offset, int32_t origin);

    uint64_t tell() {
        return pos;
    };

    uint64_t size() {
        return filesize;
    };

    void rewind() {
        this->seek(0, SEEK_SET);
    };

protected:
    int32_t iFd;
    const uint8_t *mem_file;
    uint64_t filesize;
    uint64_t pos;
};

#endif
