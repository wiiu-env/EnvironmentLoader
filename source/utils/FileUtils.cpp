#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROUNDDOWN(val, align) ((val) & ~(align - 1))
#define ROUNDUP(val, align)   ROUNDDOWN(((val) + (align - 1)), align)

int32_t LoadFileToMem(const char *filepath, uint8_t **inbuffer, uint32_t *size) {
    //! always initialze input
    *inbuffer = NULL;
    if (size) {
        *size = 0;
    }

    int32_t iFd = open(filepath, O_RDONLY);
    if (iFd < 0) {
        return -1;
    }

    uint32_t filesize = lseek(iFd, 0, SEEK_END);
    lseek(iFd, 0, SEEK_SET);

    auto *buffer = (uint8_t *) memalign(0x40, ROUNDUP(filesize, 0x40));
    if (buffer == nullptr) {
        close(iFd);
        return -2;
    }

    uint32_t blocksize = 0x20000;
    uint32_t done      = 0;
    int32_t readBytes  = 0;

    while (done < filesize) {
        if (done + blocksize > filesize) {
            blocksize = filesize - done;
        }
        readBytes = read(iFd, buffer + done, blocksize);
        if (readBytes <= 0)
            break;
        done += readBytes;
    }

    ::close(iFd);

    if (done != filesize) {
        free(buffer);
        buffer = nullptr;
        return -3;
    }

    *inbuffer = buffer;

    //! sign is optional input
    if (size) {
        *size = filesize;
    }

    return filesize;
}
