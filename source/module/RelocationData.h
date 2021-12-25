/****************************************************************************
 * Copyright (C) 2018 Maschell
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

#include <string>
#include <memory>
#include <utility>
#include "ImportRPLInformation.h"

class RelocationData {

public:
    RelocationData(char type, size_t offset, int32_t addend, void *destination, std::string name, std::shared_ptr<ImportRPLInformation> rplInfo) : rplInfo(std::move(rplInfo)) {
        this->type = type;
        this->offset = offset;
        this->addend = addend;
        this->destination = destination;
        this->name = std::move(name);
    }

    ~RelocationData() = default;

    [[nodiscard]] char getType() const {
        return type;
    }

    [[nodiscard]] size_t getOffset() const {
        return offset;
    }

    [[nodiscard]] int32_t getAddend() const {
        return addend;
    }

    [[nodiscard]] void *getDestination() const {
        return destination;
    }

    [[nodiscard]] std::string getName() const {
        return name;
    }

    [[nodiscard]] std::shared_ptr<ImportRPLInformation> getImportRPLInformation() const {
        return rplInfo;
    }

    [[nodiscard]] std::string toString() const;

private:
    char type;
    size_t offset;
    int32_t addend;
    void *destination;
    std::string name;
    const std::shared_ptr<ImportRPLInformation> rplInfo;
};
