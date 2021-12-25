/****************************************************************************
 * Copyright (C) 2018-2020 Maschell
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

#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

#define DYN_LINK_FUNCTION_NAME_LENGTH           351
#define DYN_LINK_IMPORT_NAME_LENGTH             50

#define DYN_LINK_FUNCTION_LIST_LENGTH           500
#define DYN_LINK_IMPORT_LIST_LENGTH             50

#define DYN_LINK_TRAMPOLIN_LIST_LENGTH          DYN_LINK_FUNCTION_LIST_LENGTH

typedef struct _dyn_linking_function_t {
    char functionName[DYN_LINK_FUNCTION_NAME_LENGTH + 1];
    void *address;
} dyn_linking_function_t;

typedef struct _dyn_linking_import_t {
    char importName[DYN_LINK_IMPORT_NAME_LENGTH + 1]{};
    bool isData = false;
} dyn_linking_import_t;

typedef struct _dyn_linking_relocation_entry_t {
    dyn_linking_function_t *functionEntry = nullptr;
    dyn_linking_import_t *importEntry = nullptr;
    void *destination = NULL;
    char type{};
    size_t offset{};
    int32_t addend{};
} dyn_linking_relocation_entry_t;

typedef struct _dyn_linking_relocation_data_t {
    dyn_linking_function_t functions[DYN_LINK_FUNCTION_LIST_LENGTH]{};
    dyn_linking_import_t imports[DYN_LINK_IMPORT_LIST_LENGTH];
} dyn_linking_relocation_data_t;

#ifdef __cplusplus
}
#endif
