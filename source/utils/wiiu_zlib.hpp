#pragma once

/****************************************************************************
 * Copyright (C) 2018-2020 Nathan Strong
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include "elfio/elfio_utils.hpp"
#include "logger.h"
#include "utils.h"
#include <memory>
#include <zlib.h>

class wiiu_zlib : public ELFIO::compression_interface {
public:
    std::unique_ptr<char[]> inflate(const char *data, const ELFIO::endianess_convertor *convertor, ELFIO::Elf_Xword compressed_size, ELFIO::Elf_Xword &uncompressed_size) const override {
        read_uncompressed_size(data, convertor, uncompressed_size);
        auto result = make_unique_nothrow<char[]>((uint32_t) (uncompressed_size + 1));
        if (result == nullptr) {
            return nullptr;
        }

        int z_ret;
        z_stream s = {};

        s.zalloc = Z_NULL;
        s.zfree  = Z_NULL;
        s.opaque = Z_NULL;

        if (inflateInit_(&s, ZLIB_VERSION, sizeof(s)) != Z_OK) {
            return nullptr;
        }

        s.avail_in  = compressed_size - 4;
        s.next_in   = (Bytef *) data;
        s.avail_out = uncompressed_size;
        s.next_out  = (Bytef *) result.get();

        z_ret = ::inflate(&s, Z_FINISH);
        inflateEnd(&s);

        if (z_ret != Z_OK && z_ret != Z_STREAM_END) {
            return nullptr;
        }

        result[uncompressed_size] = '\0';
        return result;
    }

    std::unique_ptr<char[]> deflate(const char *data, const ELFIO::endianess_convertor *convertor, ELFIO::Elf_Xword decompressed_size, ELFIO::Elf_Xword &compressed_size) const override {
        auto result = make_unique_nothrow<char[]>((uint32_t) (decompressed_size));
        if (result == nullptr) {
            return nullptr;
        }

        int z_ret;
        z_stream s = {};

        s.zalloc = Z_NULL;
        s.zfree  = Z_NULL;
        s.opaque = Z_NULL;

        if ((z_ret = deflateInit(&s, Z_DEFAULT_COMPRESSION)) != Z_OK) {
            return nullptr;
        }

        s.avail_in  = decompressed_size;
        s.next_in   = (Bytef *) data;
        s.avail_out = decompressed_size - 4;
        s.next_out  = (Bytef *) result.get() + 4;

        z_ret           = ::deflate(&s, Z_FINISH);
        compressed_size = decompressed_size - s.avail_out;
        deflateEnd(&s);

        if (z_ret != Z_OK && z_ret != Z_STREAM_END) {
            compressed_size = 0;
            return nullptr;
        }

        write_compressed_size(result, convertor, compressed_size);
        result[compressed_size] = '\0';
        return result;
    }

private:
    static void read_uncompressed_size(const char *&data, const ELFIO::endianess_convertor *convertor, ELFIO::Elf_Xword &uncompressed_size) {
        union _int32buffer {
            uint32_t word;
            char bytes[4];
        } int32buffer;
        memcpy(int32buffer.bytes, data, 4);
        data += 4;
        uncompressed_size = (*convertor)(int32buffer.word);
    }

    static void write_compressed_size(std::unique_ptr<char[]> &result, const ELFIO::endianess_convertor *convertor, ELFIO::Elf_Xword compressed_size) {
        union _int32buffer {
            uint32_t word;
            char bytes[4];
        } int32buffer;

        int32buffer.word = (*convertor)(compressed_size);
        memcpy(result.get(), int32buffer.bytes, 4);
    }
};