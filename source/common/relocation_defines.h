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

typedef enum RelocationTrampolinStatus {
    RELOC_TRAMP_FREE               = 0,
    RELOC_TRAMP_FIXED              = 1,
    RELOC_TRAMP_IMPORT_IN_PROGRESS = 2,
    RELOC_TRAMP_IMPORT_DONE        = 3,
} RelocationTrampolinStatus;

typedef enum RelocationType {
    RELOC_TYPE_FIXED  = 0,
    RELOC_TYPE_IMPORT = 1
} RelocationType;

typedef struct relocation_trampolin_entry_t {
    uint32_t trampolin[4];
    RelocationTrampolinStatus status;
} relocation_trampolin_entry_t;