/****************************************************************************
 * Copyright (C) 2019 Maschell
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

#include "../common/relocation_defines.h"
#include "ModuleData.h"
#include "elfio/elfio.hpp"
#include "utils/MemoryUtils.h"
#include "utils/utils.h"
#include <map>
#include <string>
#include <vector>

class ModuleDataFactory {
public:
    static uint32_t GetSizeOfModule(const ELFIO::elfio &reader);

    static std::optional<std::unique_ptr<ModuleData>> load(const ELFIO::elfio &reader, const HeapWrapper &heapWrapper, relocation_trampoline_entry_t *trampoline_data, uint32_t trampoline_data_length);

    static bool linkSection(const ELFIO::elfio &reader, uint32_t section_index, uint32_t destination, uint32_t base_text, uint32_t base_data, relocation_trampoline_entry_t *trampoline_data,
                            uint32_t trampoline_data_length);

    static bool getImportRelocationData(std::unique_ptr<ModuleData> &moduleData, const ELFIO::elfio &reader, uint8_t **destinations);
};
