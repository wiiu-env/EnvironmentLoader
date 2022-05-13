/****************************************************************************
 * Copyright (C) 2018-2021 Maschell
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

#include "RelocationData.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class ModuleData {
public:
    ModuleData() = default;

    ~ModuleData() = default;

    void setEntrypoint(uint32_t address) {
        this->entrypoint = address;
    }

    void addRelocationData(std::unique_ptr<RelocationData> relocation_data) {
        relocation_data_list.push_back(std::move(relocation_data));
    }

    [[nodiscard]] const std::vector<std::unique_ptr<RelocationData>> &getRelocationDataList() const {
        return relocation_data_list;
    }

    [[nodiscard]] uint32_t getEntrypoint() const {
        return entrypoint;
    }

    void setStartAddress(uint32_t address) {
        this->startAddress = address;
    }

    void setEndAddress(uint32_t address) {
        this->endAddress = address;
    }

    [[nodiscard]] uint32_t getStartAddress() const {
        return startAddress;
    }

    [[nodiscard]] uint32_t getEndAddress() const {
        return endAddress;
    }

private:
    std::vector<std::unique_ptr<RelocationData>> relocation_data_list;
    uint32_t entrypoint   = 0;
    uint32_t startAddress = 0;
    uint32_t endAddress   = 0;
};
