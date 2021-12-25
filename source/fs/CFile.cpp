
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <fs/CFile.hpp>

CFile::CFile() {
    iFd = -1;
    mem_file = NULL;
    filesize = 0;
    pos = 0;
}

CFile::CFile(const std::string &filepath, eOpenTypes mode) {
    iFd = -1;
    this->open(filepath, mode);
}

CFile::CFile(const uint8_t *mem, int32_t size) {
    iFd = -1;
    this->open(mem, size);
}

CFile::~CFile() {
    this->close();
}

int32_t CFile::open(const std::string &filepath, eOpenTypes mode) {
    this->close();
    int32_t openMode = 0;

    // This depend on the devoptab implementation.
    // see https://github.com/devkitPro/wut/blob/master/libraries/wutdevoptab/devoptab_fs_open.c#L21 fpr reference

    switch (mode) {
        default:
        case ReadOnly:   // file must exist
            openMode = O_RDONLY;
            break;
        case WriteOnly: // file will be created / zerod
            openMode = O_TRUNC | O_CREAT | O_WRONLY;
            break;
        case ReadWrite: // file must exist
            openMode = O_RDWR;
            break;
        case Append: // append to file, file will be created if missing. write only
            openMode = O_CREAT | O_APPEND | O_WRONLY;
            break;
    }

    //! Using fopen works only on the first launch as expected
    //! on the second launch it causes issues because we don't overwrite
    //! the .data sections which is needed for a normal application to re-init
    //! this will be added with launching as RPX
    iFd = ::open(filepath.c_str(), openMode);
    if (iFd < 0)
        return iFd;


    filesize = ::lseek(iFd, 0, SEEK_END);
    ::lseek(iFd, 0, SEEK_SET);

    return 0;
}

int32_t CFile::open(const uint8_t *mem, int32_t size) {
    this->close();

    mem_file = mem;
    filesize = size;

    return 0;
}

void CFile::close() {
    if (iFd >= 0)
        ::close(iFd);

    iFd = -1;
    mem_file = NULL;
    filesize = 0;
    pos = 0;
}

int32_t CFile::read(uint8_t *ptr, size_t size) {
    if (iFd >= 0) {
        int32_t ret = ::read(iFd, ptr, size);
        if (ret > 0)
            pos += ret;
        return ret;
    }

    int32_t readsize = size;

    if (readsize > (int64_t) (filesize - pos))
        readsize = filesize - pos;

    if (readsize <= 0)
        return readsize;

    if (mem_file != NULL) {
        memcpy(ptr, mem_file + pos, readsize);
        pos += readsize;
        return readsize;
    }

    return -1;
}

int32_t CFile::write(const uint8_t *ptr, size_t size) {
    if (iFd >= 0) {
        size_t done = 0;
        while (done < size) {
            int32_t ret = ::write(iFd, ptr, size - done);
            if (ret <= 0)
                return ret;

            ptr += ret;
            done += ret;
            pos += ret;
        }
        return done;
    }

    return -1;
}

int32_t CFile::seek(long int offset, int32_t origin) {
    int32_t ret = 0;
    int64_t newPos = pos;

    if (origin == SEEK_SET) {
        newPos = offset;
    } else if (origin == SEEK_CUR) {
        newPos += offset;
    } else if (origin == SEEK_END) {
        newPos = filesize + offset;
    }

    if (newPos < 0) {
        pos = 0;
    } else {
        pos = newPos;
    }

    if (iFd >= 0)
        ret = ::lseek(iFd, pos, SEEK_SET);

    if (mem_file != NULL) {
        if (pos > filesize) {
            pos = filesize;
        }
    }

    return ret;
}

int32_t CFile::fwrite(const char *format, ...) {
    char tmp[512];
    tmp[0] = 0;
    int32_t result = -1;

    va_list va;
    va_start(va, format);
    if ((vsprintf(tmp, format, va) >= 0)) {
        result = this->write((uint8_t *) tmp, strlen(tmp));
    }
    va_end(va);


    return result;
}


