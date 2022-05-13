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

#include "ModuleDataFactory.h"
#include "../utils/FileUtils.h"
#include "ElfUtils.h"
#include <coreinit/cache.h>
#include <map>
#include <string>
#include <vector>

using namespace ELFIO;

std::optional<std::shared_ptr<ModuleData>>
ModuleDataFactory::load(const std::string &path, uint32_t destination_address_end, uint32_t maximum_size, relocation_trampolin_entry_t *trampolin_data, uint32_t trampolin_data_length) {
    elfio reader;
    std::shared_ptr<ModuleData> moduleData = std::make_shared<ModuleData>();

    uint8_t *buffer = nullptr;
    uint32_t fsize  = 0;
    if (LoadFileToMem(path.c_str(), &buffer, &fsize) < 0) {
        DEBUG_FUNCTION_LINE_ERR("Failed to load file");
        return {};
    }

    // Load ELF data
    if (!reader.load(reinterpret_cast<char *>(buffer), fsize)) {
        DEBUG_FUNCTION_LINE_ERR("Can't find or process %s", path.c_str());
        free(buffer);
        return {};
    }

    uint32_t sec_num    = reader.sections.size();
    auto **destinations = (uint8_t **) malloc(sizeof(uint8_t *) * sec_num);

    uint32_t sizeOfModule = 0;
    for (uint32_t i = 0; i < sec_num; ++i) {
        section *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002) {
            continue;
        }

        if ((psec->get_type() == SHT_PROGBITS || psec->get_type() == SHT_NOBITS) && (psec->get_flags() & SHF_ALLOC)) {
            sizeOfModule += psec->get_size() + 1;
        }
    }

    if (sizeOfModule > maximum_size) {
        DEBUG_FUNCTION_LINE_ERR("Module is too big.");
        free(destinations);
        free(buffer);
        return {};
    }

    uint32_t baseOffset   = (destination_address_end - sizeOfModule) & 0xFFFFFF00;
    uint32_t startAddress = baseOffset;

    uint32_t offset_text = baseOffset;
    uint32_t offset_data = offset_text;

    uint32_t entrypoint = offset_text + (uint32_t) reader.get_entry() - 0x02000000;

    uint32_t totalSize  = 0;
    uint32_t endAddress = 0;

    for (uint32_t i = 0; i < sec_num; ++i) {
        section *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002) {
            continue;
        }

        if ((psec->get_type() == SHT_PROGBITS || psec->get_type() == SHT_NOBITS) && (psec->get_flags() & SHF_ALLOC)) {
            uint32_t sectionSize = psec->get_size();

            totalSize += sectionSize;
            if (totalSize > maximum_size) {
                DEBUG_FUNCTION_LINE_ERR("Couldn't load setup module because it's too big.");
                free(destinations);
                free(buffer);
                return {};
            }

            auto address = (uint32_t) psec->get_address();

            destinations[psec->get_index()] = (uint8_t *) baseOffset;

            uint32_t destination = baseOffset + address;
            if ((address >= 0x02000000) && address < 0x10000000) {
                destination -= 0x02000000;
                destinations[psec->get_index()] -= 0x02000000;
                baseOffset += sectionSize;
                offset_data += sectionSize;
            } else if ((address >= 0x10000000) && address < 0xC0000000) {
                destination -= 0x10000000;
                destinations[psec->get_index()] -= 0x10000000;
            } else if (address >= 0xC0000000) {
                destination -= 0xC0000000;
                destinations[psec->get_index()] -= 0xC0000000;
            } else {
                DEBUG_FUNCTION_LINE_ERR("Unhandled case");
                free(destinations);
                free(buffer);
                return std::nullopt;
            }

            const char *p = reader.sections[i]->get_data();

            if (psec->get_type() == SHT_NOBITS) {
                DEBUG_FUNCTION_LINE("memset section %s %08X to 0 (%d bytes)", psec->get_name().c_str(), destination, sectionSize);
                memset((void *) destination, 0, sectionSize);
            } else if (psec->get_type() == SHT_PROGBITS) {
                DEBUG_FUNCTION_LINE("Copy section %s %08X -> %08X (%d bytes)", psec->get_name().c_str(), p, destination, sectionSize);
                memcpy((void *) destination, p, sectionSize);
            }

            //nextAddress = ROUNDUP(destination + sectionSize, 0x100);
            if (psec->get_name() == ".bss") {
                DEBUG_FUNCTION_LINE("memset %s section. Location: %08X size: %08X", psec->get_name().c_str(), destination, sectionSize);
                memset(reinterpret_cast<void *>(destination), 0, sectionSize);
            } else if (psec->get_name() == ".sbss") {
                DEBUG_FUNCTION_LINE("memset %s section. Location: %08X size: %08X", psec->get_name().c_str(), destination, sectionSize);
                memset(reinterpret_cast<void *>(destination), 0, sectionSize);
            }

            if (endAddress < destination + sectionSize) {
                endAddress = destination + sectionSize;
            }

            DCFlushRange((void *) destination, sectionSize);
            ICInvalidateRange((void *) destination, sectionSize);
        }
    }

    for (uint32_t i = 0; i < sec_num; ++i) {
        section *psec = reader.sections[i];
        if ((psec->get_type() == SHT_PROGBITS || psec->get_type() == SHT_NOBITS) && (psec->get_flags() & SHF_ALLOC)) {
            DEBUG_FUNCTION_LINE("Linking (%d)... %s", i, psec->get_name().c_str());
            if (!linkSection(reader, psec->get_index(), (uint32_t) destinations[psec->get_index()], offset_text, offset_data, trampolin_data, trampolin_data_length)) {
                DEBUG_FUNCTION_LINE_ERR("elfLink failed");
                free(destinations);
                free(buffer);
                return std::nullopt;
            }
        }
    }
    auto relocationData = getImportRelocationData(reader, destinations);

    for (auto const &reloc : relocationData) {
        moduleData->addRelocationData(reloc);
    }

    DCFlushRange((void *) baseOffset, totalSize);
    ICInvalidateRange((void *) baseOffset, totalSize);

    free(destinations);
    free(buffer);

    moduleData->setStartAddress(startAddress);
    moduleData->setEndAddress(endAddress);
    moduleData->setEntrypoint(entrypoint);
    DEBUG_FUNCTION_LINE("Saved entrypoint as %08X", entrypoint);

    return moduleData;
}

std::vector<std::shared_ptr<RelocationData>> ModuleDataFactory::getImportRelocationData(elfio &reader, uint8_t **destinations) {
    std::vector<std::shared_ptr<RelocationData>> result;
    std::map<uint32_t, std::string> infoMap;

    uint32_t sec_num = reader.sections.size();

    for (uint32_t i = 0; i < sec_num; ++i) {
        section *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002) {
            infoMap[i] = psec->get_name();
        }
    }

    for (uint32_t i = 0; i < sec_num; ++i) {
        section *psec = reader.sections[i];
        if (psec->get_type() == SHT_RELA || psec->get_type() == SHT_REL) {
            DEBUG_FUNCTION_LINE_VERBOSE("Found relocation section %s", psec->get_name().c_str());
            relocation_section_accessor rel(reader, psec);
            for (uint32_t j = 0; j < (uint32_t) rel.get_entries_num(); ++j) {
                Elf64_Addr offset;
                Elf_Word type;
                Elf_Sxword addend;
                std::string sym_name;
                Elf64_Addr sym_value;
                Elf_Half sym_section_index;

                if (!rel.get_entry(j, offset, sym_value, sym_name, type, addend, sym_section_index)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to get relocation");
                    break;
                }

                // uint32_t adjusted_sym_value = (uint32_t) sym_value;
                if (infoMap.count(sym_section_index) == 0) {
                    continue;
                }
                auto rplInfo = ImportRPLInformation::createImportRPLInformation(infoMap[sym_section_index]);
                if (!rplInfo) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to create import information");
                    break;
                }

                uint32_t section_index = psec->get_info();

                // When these relocations are performed, we don't need the 0xC0000000 offset anymore.
                auto relocationData = std::make_shared<RelocationData>(type, offset - 0x02000000, addend, (void *) (destinations[section_index] + 0x02000000), sym_name, rplInfo.value());
                //relocationData->printInformation();
                result.push_back(relocationData);
            }
        }
    }
    return result;
}

bool ModuleDataFactory::linkSection(elfio &reader, uint32_t section_index, uint32_t destination, uint32_t base_text, uint32_t base_data, relocation_trampolin_entry_t *trampolin_data,
                                    uint32_t trampolin_data_length) {
    uint32_t sec_num = reader.sections.size();

    for (uint32_t i = 0; i < sec_num; ++i) {
        section *psec = reader.sections[i];
        if (psec->get_info() == section_index) {
            DEBUG_FUNCTION_LINE_VERBOSE("Found relocation section %s", psec->get_name().c_str());
            relocation_section_accessor rel(reader, psec);
            for (uint32_t j = 0; j < (uint32_t) rel.get_entries_num(); ++j) {
                Elf64_Addr offset;
                Elf_Word type;
                Elf_Sxword addend;
                std::string sym_name;
                Elf64_Addr sym_value;
                Elf_Half sym_section_index;

                if (!rel.get_entry(j, offset, sym_value, sym_name, type, addend, sym_section_index)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to get relocation");
                    break;
                }

                auto adjusted_sym_value = (uint32_t) sym_value;
                if ((adjusted_sym_value >= 0x02000000) && adjusted_sym_value < 0x10000000) {
                    adjusted_sym_value -= 0x02000000;
                    adjusted_sym_value += base_text;
                } else if ((adjusted_sym_value >= 0x10000000) && adjusted_sym_value < 0xC0000000) {
                    adjusted_sym_value -= 0x10000000;
                    adjusted_sym_value += base_data;
                } else if (adjusted_sym_value >= 0xC0000000) {
                    // Skip imports
                    continue;
                } else if (adjusted_sym_value == 0x0) {
                    //
                } else {
                    DEBUG_FUNCTION_LINE_ERR("Unhandled case %08X", adjusted_sym_value);
                    return false;
                }

                if (sym_section_index == SHN_ABS) {
                    //
                } else if (sym_section_index > SHN_LORESERVE) {
                    DEBUG_FUNCTION_LINE_ERR("NOT IMPLEMENTED: %04X", sym_section_index);
                    return false;
                }

                if (!ElfUtils::elfLinkOne(type, offset, addend, destination, adjusted_sym_value, trampolin_data, trampolin_data_length, RELOC_TYPE_FIXED)) {
                    DEBUG_FUNCTION_LINE_ERR("Link failed");
                    return false;
                }
            }
            DEBUG_FUNCTION_LINE_VERBOSE("done");
        }
    }
    return true;
}
