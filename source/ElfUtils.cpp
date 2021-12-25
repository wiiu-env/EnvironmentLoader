#include <coreinit/debug.h>
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <whb/sdcard.h>
#include <whb/file.h>
#include <whb/log.h>
#include <bits/shared_ptr.h>
#include <coreinit/dynload.h>
#include "utils/logger.h"

#include "elfio/elfio.hpp"
#include "ElfUtils.h"

int32_t LoadFileToMem(const char *relativefilepath, char **fileOut, uint32_t *sizeOut) {
    char path[256];
    int result = 0;
    char *sdRootPath = nullptr;
    if (!WHBMountSdCard()) {
        DEBUG_FUNCTION_LINE("Failed to mount SD Card...");
        result = -1;
        goto exit;
    }

    sdRootPath = WHBGetSdCardMountPath();
    sprintf(path, "%s/%s", sdRootPath, relativefilepath);

    DEBUG_FUNCTION_LINE("Loading file %s.", path);

    *fileOut = WHBReadWholeFile(path, sizeOut);
    if (!(*fileOut)) {
        result = -2;
        DEBUG_FUNCTION_LINE("WHBReadWholeFile(%s) returned NULL", path);
        goto exit;
    }

    exit:
    WHBUnmountSdCard();
    return result;
}

uint32_t load_loader_elf_from_sd(unsigned char *baseAddress, const char *relativePath) {
    char *elf_data = nullptr;
    uint32_t fileSize = 0;
    if (LoadFileToMem(relativePath, &elf_data, &fileSize) != 0) {
        OSFatal("Failed to load hook_payload.elf from the SD Card.");
    }
    uint32_t result = load_loader_elf(baseAddress, elf_data, fileSize);

    MEMFreeToDefaultHeap((void *) elf_data);

    return result;
}

uint32_t load_loader_elf(unsigned char *baseAddress, char *elf_data, uint32_t fileSize) {
    ELFIO::Elf32_Ehdr *ehdr;
    ELFIO::Elf32_Phdr *phdrs;
    uint8_t *image;
    int32_t i;

    ehdr = (ELFIO::Elf32_Ehdr *) elf_data;

    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        return 0;
    }

    if (ehdr->e_phentsize != sizeof(ELFIO::Elf32_Phdr)) {
        return 0;
    }

    phdrs = (ELFIO::Elf32_Phdr *) (elf_data + ehdr->e_phoff);

    for (i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) {
            continue;
        }

        if (phdrs[i].p_filesz > phdrs[i].p_memsz) {
            continue;
        }

        if (!phdrs[i].p_filesz) {
            continue;
        }

        uint32_t p_paddr = phdrs[i].p_paddr + (uint32_t) baseAddress;
        image = (uint8_t *) (elf_data + phdrs[i].p_offset);

        memcpy((void *) p_paddr, image, phdrs[i].p_filesz);
        DCFlushRange((void *) p_paddr, phdrs[i].p_filesz);

        if (phdrs[i].p_flags & PF_X) {
            ICInvalidateRange((void *) p_paddr, phdrs[i].p_memsz);
        }
    }

    //! clear BSS
    auto *shdr = (ELFIO::Elf32_Shdr *) (elf_data + ehdr->e_shoff);
    for (i = 0; i < ehdr->e_shnum; i++) {
        const char *section_name = ((const char *) elf_data) + shdr[ehdr->e_shstrndx].sh_offset + shdr[i].sh_name;
        if (section_name[0] == '.' && section_name[1] == 'b' && section_name[2] == 's' && section_name[3] == 's') {
            memset((void *) (shdr[i].sh_addr + baseAddress), 0, shdr[i].sh_size);
            DCFlushRange((void *) (shdr[i].sh_addr + baseAddress), shdr[i].sh_size);
        } else if (section_name[0] == '.' && section_name[1] == 's' && section_name[2] == 'b' && section_name[3] == 's' && section_name[4] == 's') {
            memset((void *) (shdr[i].sh_addr + baseAddress), 0, shdr[i].sh_size);
            DCFlushRange((void *) (shdr[i].sh_addr + baseAddress), shdr[i].sh_size);
        }
    }

    return ehdr->e_entry;
}


bool ElfUtils::doRelocation(std::vector<std::shared_ptr<RelocationData>> &relocData, relocation_trampolin_entry_t *tramp_data, uint32_t tramp_length) {
    for (auto const &curReloc: relocData) {
        std::string functionName = curReloc->getName();
        std::string rplName = curReloc->getImportRPLInformation()->getName();
        int32_t isData = curReloc->getImportRPLInformation()->isData();
        OSDynLoad_Module rplHandle = nullptr;

        if (OSDynLoad_IsModuleLoaded(rplName.c_str(), &rplHandle) != OS_DYNLOAD_OK) {
            // only acquire if not already loaded.
            OSDynLoad_Acquire(rplName.c_str(), &rplHandle);
        }

        uint32_t functionAddress = 0;
        OSDynLoad_FindExport(rplHandle, isData, functionName.c_str(), (void **) &functionAddress);
        if (functionAddress == 0) {
            return false;
        }
        if (!ElfUtils::elfLinkOne(curReloc->getType(), curReloc->getOffset(), curReloc->getAddend(), (uint32_t) curReloc->getDestination(), functionAddress, tramp_data, tramp_length,
                                  RELOC_TYPE_IMPORT)) {
            DEBUG_FUNCTION_LINE("Relocation failed\n");
            return false;
        }
    }

    DCFlushRange(tramp_data, tramp_length * sizeof(relocation_trampolin_entry_t));
    ICInvalidateRange(tramp_data, tramp_length * sizeof(relocation_trampolin_entry_t));
    return true;
}

// See https://github.com/decaf-emu/decaf-emu/blob/43366a34e7b55ab9d19b2444aeb0ccd46ac77dea/src/libdecaf/src/cafe/loader/cafe_loader_reloc.cpp#L144
bool ElfUtils::elfLinkOne(char type, size_t offset, int32_t addend, uint32_t destination, uint32_t symbol_addr, relocation_trampolin_entry_t *trampolin_data, uint32_t trampolin_data_length,
                          RelocationType reloc_type) {
    if (type == R_PPC_NONE) {
        return true;
    }
    auto target = destination + offset;
    auto value = symbol_addr + addend;


    auto relValue = value - static_cast<uint32_t>(target);

    switch (type) {
        case R_PPC_NONE:
            break;
        case R_PPC_ADDR32:
            *((uint32_t *) (target)) = value;
            break;
        case R_PPC_ADDR16_LO:
            *((uint16_t *) (target)) = static_cast<uint16_t>(value & 0xFFFF);
            break;
        case R_PPC_ADDR16_HI:
            *((uint16_t *) (target)) = static_cast<uint16_t>(value >> 16);
            break;
        case R_PPC_ADDR16_HA:
            *((uint16_t *) (target)) = static_cast<uint16_t>((value + 0x8000) >> 16);
            break;
        case R_PPC_DTPMOD32:
            DEBUG_FUNCTION_LINE("################IMPLEMENT ME\n");
            //*((int32_t *)(target)) = tlsModuleIndex;
            break;
        case R_PPC_DTPREL32:
            *((uint32_t *) (target)) = value;
            break;
        case R_PPC_GHS_REL16_HA:
            *((uint16_t *) (target)) = static_cast<uint16_t>((relValue + 0x8000) >> 16);
            break;
        case R_PPC_GHS_REL16_HI:
            *((uint16_t *) (target)) = static_cast<uint16_t>(relValue >> 16);
            break;
        case R_PPC_GHS_REL16_LO:
            *((uint16_t *) (target)) = static_cast<uint16_t>(relValue & 0xFFFF);
            break;
        case R_PPC_REL14: {
            auto distance = static_cast<int32_t>(value) - static_cast<int32_t>(target);
            if (distance > 0x7FFC || distance < -0x7FFC) {
                DEBUG_FUNCTION_LINE("***14-bit relative branch cannot hit target.");
                return false;
            }

            if (distance & 3) {
                DEBUG_FUNCTION_LINE("***RELOC ERROR %d: lower 2 bits must be zero before shifting.", -470040);
                return false;
            }

            if ((distance >= 0 && (distance & 0xFFFF8000)) ||
                (distance < 0 && ((distance & 0xFFFF8000) != 0xFFFF8000))) {
                DEBUG_FUNCTION_LINE("***RELOC ERROR %d: upper 17 bits before shift must all be the same.", -470040);
                return false;
            }

            *(int32_t *) target = (*(int32_t *) target & 0xFFBF0003) | (distance & 0x0000fffc);
            break;
        }
        case R_PPC_REL24: {
            // if (isWeakSymbol && !symbolValue) {
            //     symbolValue = static_cast<uint32_t>(target);
            //     value = symbolValue + addend;
            // }
            auto distance = static_cast<int32_t>(value) - static_cast<int32_t>(target);
            if (distance > 0x1FFFFFC || distance < -0x1FFFFFC) {
                if (trampolin_data == nullptr) {
                    DEBUG_FUNCTION_LINE("***24-bit relative branch cannot hit target. Trampolin isn't provided\n");
                    DEBUG_FUNCTION_LINE("***value %08X - target %08X = distance %08X\n", value, target, distance);
                    return false;
                } else {
                    relocation_trampolin_entry_t *freeSlot = nullptr;
                    for (uint32_t i = 0; i < trampolin_data_length; i++) {
                        // We want to override "old" relocations of imports
                        // Pending relocations have the status RELOC_TRAMP_IMPORT_IN_PROGRESS.
                        // When all relocations are done successfully, they will be turned into RELOC_TRAMP_IMPORT_DONE
                        // so they can be overridden/updated/reused on the next application launch.
                        //
                        // Relocations that won't change will have the status RELOC_TRAMP_FIXED and are set to free when the module is unloaded.
                        if (trampolin_data[i].status == RELOC_TRAMP_FREE ||
                            trampolin_data[i].status == RELOC_TRAMP_IMPORT_DONE) {
                            freeSlot = &(trampolin_data[i]);
                            break;
                        }
                    }
                    if (freeSlot == nullptr) {
                        DEBUG_FUNCTION_LINE("***24-bit relative branch cannot hit target. Trampolin data list is full\n");
                        DEBUG_FUNCTION_LINE("***value %08X - target %08X = distance %08X\n", value, target, (target - (uint32_t) &(freeSlot->trampolin[0])));
                        return false;
                    }
                    if (target - (uint32_t) &(freeSlot->trampolin[0]) > 0x1FFFFFC) {
                        DEBUG_FUNCTION_LINE("**Cannot link 24-bit jump (too far to tramp buffer).");
                        DEBUG_FUNCTION_LINE("***value %08X - target %08X = distance %08X\n", value, target, (target - (uint32_t) &(freeSlot->trampolin[0])));
                        return false;
                    }

                    freeSlot->trampolin[0] = 0x3D600000 | ((((uint32_t) value) >> 16) & 0x0000FFFF); // lis r11, real_addr@h
                    freeSlot->trampolin[1] = 0x616B0000 | (((uint32_t) value) & 0x0000ffff); // ori r11, r11, real_addr@l
                    freeSlot->trampolin[2] = 0x7D6903A6; // mtctr   r11
                    freeSlot->trampolin[3] = 0x4E800420; // bctr
                    DCFlushRange((void *) freeSlot->trampolin, sizeof(freeSlot->trampolin));
                    ICInvalidateRange((unsigned char *) freeSlot->trampolin, sizeof(freeSlot->trampolin));

                    if (reloc_type == RELOC_TYPE_FIXED) {
                        freeSlot->status = RELOC_TRAMP_FIXED;
                    } else {
                        // Relocations for the imports may be overridden
                        freeSlot->status = RELOC_TRAMP_IMPORT_DONE;
                    }
                    auto symbolValue = (uint32_t) &(freeSlot->trampolin[0]);
                    value = symbolValue + addend;
                    distance = static_cast<int32_t>(value) - static_cast<int32_t>(target);
                }
            }

            if (distance & 3) {
                DEBUG_FUNCTION_LINE("***RELOC ERROR %d: lower 2 bits must be zero before shifting.", -470022);
                return false;
            }

            if (distance < 0 && (distance & 0xFE000000) != 0xFE000000) {
                DEBUG_FUNCTION_LINE("***RELOC ERROR %d: upper 7 bits before shift must all be the same (1).", -470040);
                return false;
            }

            if (distance >= 0 && (distance & 0xFE000000)) {
                DEBUG_FUNCTION_LINE("***RELOC ERROR %d: upper 7 bits before shift must all be the same (0).", -470040);
                return false;
            }

            *(int32_t *) target = (*(int32_t *) target & 0xfc000003) | (distance & 0x03fffffc);
            break;
        }
        default:
            DEBUG_FUNCTION_LINE("***ERROR: Unsupported Relocation_Add Type (%08X):", type);
            return false;
    }
    return true;
}
