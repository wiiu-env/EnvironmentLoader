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
#include "utils/OnLeavingScope.h"
#include "utils/utils.h"
#include "utils/wiiu_zlib.hpp"
#include <coreinit/cache.h>
#include <map>
#include <string>


uint32_t ModuleDataFactory::GetSizeOfModule(const ELFIO::elfio &reader) {
    uint32_t sec_num      = reader.sections.size();
    uint32_t sizeOfModule = 0;
    for (uint32_t i = 0; i < sec_num; ++i) {
        ELFIO::section *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002 || psec->get_name() == ".wut_load_bounds") {
            continue;
        }

        if ((psec->get_type() == ELFIO::SHT_PROGBITS || psec->get_type() == ELFIO::SHT_NOBITS) && (psec->get_flags() & ELFIO::SHF_ALLOC)) {
            sizeOfModule += psec->get_size() + psec->get_addr_align();
        }
    }
    return sizeOfModule;
}

std::optional<std::unique_ptr<ModuleData>>
ModuleDataFactory::load(const ELFIO::elfio &reader, const HeapWrapper &heapWrapper, relocation_trampoline_entry_t *trampoline_data, uint32_t trampoline_data_length) {
    auto moduleData = make_unique_nothrow<ModuleData>();
    if (!moduleData) {
        DEBUG_FUNCTION_LINE_ERR("Failed to allocate ModuleData");
        return {};
    }

    uint32_t sec_num = reader.sections.size();

    auto destinations = make_unique_nothrow<uint8_t *[]>(sec_num);
    if (!destinations) {
        DEBUG_FUNCTION_LINE_ERR("Failed alloc memory for destinations array");
        return {};
    }

    uint32_t text_size = 0;
    uint32_t data_size = 0;

    for (uint32_t i = 0; i < sec_num; ++i) {
        ELFIO::section *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002) {
            continue;
        }

        if ((psec->get_type() == ELFIO::SHT_PROGBITS || psec->get_type() == ELFIO::SHT_NOBITS) && (psec->get_flags() & ELFIO::SHF_ALLOC)) {
            uint32_t sectionSize = psec->get_size();
            auto address         = (uint32_t) psec->get_address();
            if ((address >= 0x02000000) && address < 0x10000000) {
                text_size += sectionSize + psec->get_addr_align();
            } else if ((address >= 0x10000000) && address < 0xC0000000) {
                data_size += sectionSize + psec->get_addr_align();
            }
        }
    }

    auto text_dataOpt = heapWrapper.Alloc(text_size, 0x100);
    if (!text_dataOpt) {
        DEBUG_FUNCTION_LINE_ERR("Failed to alloc memory for the .text section (%d bytes)", text_size);
        return std::nullopt;
    }
    ExpHeapMemory text_data = std::move(*text_dataOpt);

    auto data_dataOpt = heapWrapper.Alloc(data_size, 0x100);
    if (!data_dataOpt) {
        DEBUG_FUNCTION_LINE_ERR("Failed to alloc memory for the .data section (%d bytes)", data_size);
        return std::nullopt;
    }
    ExpHeapMemory data_data = std::move(*data_dataOpt);

    uint32_t entrypoint = (uint32_t) text_data.data() + (uint32_t) reader.get_entry() - 0x02000000;

    for (uint32_t i = 0; i < sec_num; ++i) {
        ELFIO::section *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002 || psec->get_name() == ".wut_load_bounds") {
            continue;
        }

        if ((psec->get_type() == ELFIO::SHT_PROGBITS || psec->get_type() == ELFIO::SHT_NOBITS) && (psec->get_flags() & ELFIO::SHF_ALLOC)) {
            uint32_t sectionSize = psec->get_size();
            auto address         = (uint32_t) psec->get_address();

            uint32_t destination = address;

            if ((address >= 0x02000000) && address < 0x10000000) {
                destination += (uint32_t) text_data.data();
                destination -= 0x02000000;
                destinations[psec->get_index()] = (uint8_t *) text_data.data();

                if (destination + sectionSize > (uint32_t) text_data.data() + text_size) {
                    DEBUG_FUNCTION_LINE_ERR("Tried to overflow .text buffer. %08X > %08X", destination + sectionSize, (uint32_t) text_data.data() + text_data.size());
                    OSFatal("EnvironmentLoader: Tried to overflow .text buffer");
                } else if (destination < (uint32_t) text_data.data()) {
                    DEBUG_FUNCTION_LINE_ERR("Tried to underflow .text buffer. %08X < %08X", destination, (uint32_t) text_data.data());
                    OSFatal("EnvironmentLoader: Tried to underflow .text buffer");
                }
            } else if ((address >= 0x10000000) && address < 0xC0000000) {
                destination += (uint32_t) data_data.data();
                destination -= 0x10000000;
                destinations[psec->get_index()] = (uint8_t *) data_data.data();

                if (destination + sectionSize > (uint32_t) data_data.data() + data_data.size()) {
                    DEBUG_FUNCTION_LINE_ERR("Tried to overflow .data buffer. %08X > %08X", destination + sectionSize, (uint32_t) data_data.data() + data_data.size());
                    OSFatal("EnvironmentLoader: Tried to overflow .data buffer");
                } else if (destination < (uint32_t) data_data.data()) {
                    DEBUG_FUNCTION_LINE_ERR("Tried to underflow .data buffer. %08X < %08X", destination, (uint32_t) data_data.data());
                    OSFatal("EnvironmentLoader: Tried to underflow .data buffer");
                }
            } else if (address >= 0xC0000000) {
                DEBUG_FUNCTION_LINE_ERR("Loading section from 0xC0000000 is NOT supported");
                return std::nullopt;
            } else {
                DEBUG_FUNCTION_LINE_ERR("Unhandled case");
                return std::nullopt;
            }

            const char *p = psec->get_data();

            uint32_t address_align = psec->get_addr_align();
            if ((destination & (address_align - 1)) != 0) {
                DEBUG_FUNCTION_LINE_WARN("Address not aligned: %08X %08X", destination, address_align);
                OSFatal("EnvironmentLoader: Address not aligned");
            }

            if (psec->get_type() == ELFIO::SHT_NOBITS) {
                DEBUG_FUNCTION_LINE_VERBOSE("memset section %s %08X to 0 (%d bytes)", psec->get_name().c_str(), destination, sectionSize);
                memset((void *) destination, 0, sectionSize);
            } else if (psec->get_type() == ELFIO::SHT_PROGBITS) {
                DEBUG_FUNCTION_LINE_VERBOSE("Copy section %s %08X -> %08X (%d bytes)", psec->get_name().c_str(), p, destination, sectionSize);
                memcpy((void *) destination, p, sectionSize);
            }
            DEBUG_FUNCTION_LINE_VERBOSE("Saved %s section info. Location: %08X size: %08X", psec->get_name().c_str(), destination, sectionSize);

            DCFlushRange((void *) destination, sectionSize);
            ICInvalidateRange((void *) destination, sectionSize);
        }
    }

    for (uint32_t i = 0; i < sec_num; ++i) {
        ELFIO::section *psec = reader.sections[i];
        if ((psec->get_type() == ELFIO::SHT_PROGBITS || psec->get_type() == ELFIO::SHT_NOBITS) && (psec->get_flags() & ELFIO::SHF_ALLOC)) {
            DEBUG_FUNCTION_LINE("Linking (%d)... %s", i, psec->get_name().c_str());
            if (!linkSection(reader, psec->get_index(), (uint32_t) destinations[psec->get_index()], (uint32_t) (text_data.data()), (uint32_t) (data_data.data()), trampoline_data, trampoline_data_length)) {
                DEBUG_FUNCTION_LINE_ERR("elfLink failed");
                return std::nullopt;
            }
        }
    }
    getImportRelocationData(moduleData, reader, destinations.get());

    DCFlushRange((void *) data_data.data(), data_data.size());
    ICInvalidateRange((void *) text_data.data(), text_data.size());

    moduleData->setEntrypoint(entrypoint);
    moduleData->setTextMemory(std::move(text_data));
    moduleData->setDataMemory(std::move(data_data));

    DEBUG_FUNCTION_LINE("Saved entrypoint as %08X", entrypoint);

    return moduleData;
}

bool ModuleDataFactory::getImportRelocationData(std::unique_ptr<ModuleData> &moduleData, const ELFIO::elfio &reader, uint8_t **destinations) {
    std::map<uint32_t, std::shared_ptr<ImportRPLInformation>> infoMap;

    uint32_t sec_num = reader.sections.size();

    for (uint32_t i = 0; i < sec_num; ++i) {
        auto *psec = reader.sections[i];
        if (psec->get_type() == 0x80000002) {
            auto info = make_shared_nothrow<ImportRPLInformation>(psec->get_name());
            if (!info) {
                DEBUG_FUNCTION_LINE_ERR("Failed too allocate ImportRPLInformation");
                return false;
            }
            infoMap[i] = std::move(info);
        }
    }

    for (uint32_t i = 0; i < sec_num; ++i) {
        ELFIO::section *psec = reader.sections[i];
        if (psec->get_type() == ELFIO::SHT_RELA || psec->get_type() == ELFIO::SHT_REL) {
            DEBUG_FUNCTION_LINE_VERBOSE("Found relocation section %s", psec->get_name().c_str());
            ELFIO::relocation_section_accessor rel(reader, psec);
            for (uint32_t j = 0; j < (uint32_t) rel.get_entries_num(); ++j) {
                ELFIO::Elf_Word symbol = 0;
                ELFIO::Elf64_Addr offset;
                ELFIO::Elf_Word type;
                ELFIO::Elf_Sxword addend;
                std::string sym_name;
                ELFIO::Elf64_Addr sym_value;

                if (!rel.get_entry(j, offset, symbol, type, addend)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to get relocation");
                    return false;
                }
                ELFIO::symbol_section_accessor symbols(reader, reader.sections[(ELFIO::Elf_Half) psec->get_link()]);

                // Find the symbol
                ELFIO::Elf_Xword size;
                unsigned char bind;
                unsigned char symbolType;
                ELFIO::Elf_Half sym_section_index;
                unsigned char other;

                if (!symbols.get_symbol(symbol, sym_name, sym_value, size,
                                        bind, symbolType, sym_section_index, other)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to get symbol");
                    return false;
                }

                auto adjusted_sym_value = (uint32_t) sym_value;
                if (adjusted_sym_value < 0xC0000000) {
                    continue;
                }

                uint32_t section_index = psec->get_info();
                if (!infoMap.contains(sym_section_index)) {
                    DEBUG_FUNCTION_LINE_ERR("Relocation is referencing a unknown section. %d destination: %08X sym_name %s", section_index, destinations[section_index], sym_name.c_str());
                    OSFatal("EnvironmentLoader: Relocation is referencing a unknown section.");
                    return false;
                }

                moduleData->addRelocationData(RelocationData(type,
                                                             offset - 0x02000000,
                                                             addend,
                                                             (void *) (destinations[section_index]),
                                                             sym_name,
                                                             infoMap[sym_section_index]));
            }
        }
    }
    return true;
}

bool ModuleDataFactory::linkSection(const ELFIO::elfio &reader, uint32_t section_index, uint32_t destination, uint32_t base_text, uint32_t base_data, relocation_trampoline_entry_t *trampoline_data,
                                    uint32_t trampoline_data_length) {
    uint32_t sec_num = reader.sections.size();

    for (uint32_t i = 0; i < sec_num; ++i) {
        ELFIO::section *psec = reader.sections[i];
        if (psec->get_info() == section_index) {
            DEBUG_FUNCTION_LINE_VERBOSE("Found relocation section %s", psec->get_name().c_str());
            ELFIO::relocation_section_accessor rel(reader, psec);
            for (uint32_t j = 0; j < (uint32_t) rel.get_entries_num(); ++j) {
                ELFIO::Elf_Word symbol = 0;
                ELFIO::Elf64_Addr offset;
                ELFIO::Elf_Word type;
                ELFIO::Elf_Sxword addend;
                std::string sym_name;
                ELFIO::Elf64_Addr sym_value;

                if (!rel.get_entry(j, offset, symbol, type, addend)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to get relocation");
                    return false;
                }
                ELFIO::symbol_section_accessor symbols(reader, reader.sections[(ELFIO::Elf_Half) psec->get_link()]);

                // Find the symbol
                ELFIO::Elf_Xword size;
                unsigned char bind;
                unsigned char symbolType;
                ELFIO::Elf_Half sym_section_index;
                unsigned char other;

                if (!symbols.get_symbol(symbol, sym_name, sym_value, size,
                                        bind, symbolType, sym_section_index, other)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to get symbol");
                    return false;
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

                auto adjusted_offset = (uint32_t) offset;
                if ((offset >= 0x02000000) && offset < 0x10000000) {
                    adjusted_offset -= 0x02000000;
                } else if ((adjusted_offset >= 0x10000000) && adjusted_offset < 0xC0000000) {
                    adjusted_offset -= 0x10000000;
                } else if (adjusted_offset >= 0xC0000000) {
                    adjusted_offset -= 0xC0000000;
                }

                if (sym_section_index == ELFIO::SHN_ABS) {
                    //
                } else if (sym_section_index > ELFIO::SHN_LORESERVE) {
                    DEBUG_FUNCTION_LINE_ERR("NOT IMPLEMENTED: %04X", sym_section_index);
                    return false;
                }
                if (!ElfUtils::elfLinkOne(type, adjusted_offset, addend, destination, adjusted_sym_value, trampoline_data, trampoline_data_length, RELOC_TYPE_FIXED)) {
                    DEBUG_FUNCTION_LINE_ERR("Link failed");
                    return false;
                }
            }
            return true;
        }
    }
    return true;
}
