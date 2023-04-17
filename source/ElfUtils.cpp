#include "utils/logger.h"
#include <bits/shared_ptr.h>
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
#include <coreinit/memdefaultheap.h>

#include "ElfUtils.h"
#include "elfio/elfio.hpp"

bool ElfUtils::doRelocation(const std::vector<std::unique_ptr<RelocationData>> &relocData, relocation_trampoline_entry_t *tramp_data, uint32_t tramp_length) {
    for (auto const &curReloc : relocData) {
        std::string functionName   = curReloc->getName();
        std::string rplName        = curReloc->getImportRPLInformation()->getRPLName();
        int32_t isData             = curReloc->getImportRPLInformation()->isData();
        OSDynLoad_Module rplHandle = nullptr;


        auto err = OSDynLoad_IsModuleLoaded(rplName.c_str(), &rplHandle);
        if (err != OS_DYNLOAD_OK || rplHandle == nullptr) {
            // only acquire if not already loaded.
            OSDynLoad_Acquire(rplName.c_str(), &rplHandle);
        }

        uint32_t functionAddress = 0;
        OSDynLoad_FindExport(rplHandle, (OSDynLoad_ExportType) isData, functionName.c_str(), (void **) &functionAddress);
        if (functionAddress == 0) {
            DEBUG_FUNCTION_LINE_ERR("Failed to find export for %s %s %d", functionName.c_str(), rplName.c_str(), isData);
            return false;
        }
        if (!ElfUtils::elfLinkOne(curReloc->getType(), curReloc->getOffset(), curReloc->getAddend(), (uint32_t) curReloc->getDestination(), functionAddress, tramp_data, tramp_length,
                                  RELOC_TYPE_IMPORT)) {
            DEBUG_FUNCTION_LINE_ERR("Relocation failed\n");
            return false;
        }
    }

    DCFlushRange(tramp_data, tramp_length * sizeof(relocation_trampoline_entry_t));
    ICInvalidateRange(tramp_data, tramp_length * sizeof(relocation_trampoline_entry_t));
    return true;
}

// See https://github.com/decaf-emu/decaf-emu/blob/43366a34e7b55ab9d19b2444aeb0ccd46ac77dea/src/libdecaf/src/cafe/loader/cafe_loader_reloc.cpp#L144
bool ElfUtils::elfLinkOne(char type, size_t offset, int32_t addend, uint32_t destination, uint32_t symbol_addr, relocation_trampoline_entry_t *trampoline_data, uint32_t trampoline_data_length,
                          RelocationType reloc_type) {
    if (type == R_PPC_NONE) {
        return true;
    }

    auto target = destination + offset;
    auto value  = symbol_addr + addend;

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
            DEBUG_FUNCTION_LINE_ERR("################IMPLEMENT ME");
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
                DEBUG_FUNCTION_LINE_ERR("***14-bit relative branch cannot hit target.");
                return false;
            }

            if (distance & 3) {
                DEBUG_FUNCTION_LINE_ERR("***RELOC ERROR %d: lower 2 bits must be zero before shifting.", -470040);
                return false;
            }

            if ((distance >= 0 && (distance & 0xFFFF8000)) ||
                (distance < 0 && ((distance & 0xFFFF8000) != 0xFFFF8000))) {
                DEBUG_FUNCTION_LINE_ERR("***RELOC ERROR %d: upper 17 bits before shift must all be the same.", -470040);
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
                if (trampoline_data == nullptr) {
                    DEBUG_FUNCTION_LINE_ERR("***24-bit relative branch cannot hit target. Trampoline isn't provided");
                    DEBUG_FUNCTION_LINE_ERR("***value %08X - target %08X = distance %08X", value, target, distance);
                    return false;
                } else {
                    relocation_trampoline_entry_t *freeSlot = nullptr;
                    for (uint32_t i = 0; i < trampoline_data_length; i++) {
                        // We want to override "old" relocations of imports
                        // Pending relocations have the status RELOC_TRAMP_IMPORT_IN_PROGRESS.
                        // When all relocations are done successfully, they will be turned into RELOC_TRAMP_IMPORT_DONE
                        // so they can be overridden/updated/reused on the next application launch.
                        //
                        // Relocations that won't change will have the status RELOC_TRAMP_FIXED and are set to free when the module is unloaded.
                        if (trampoline_data[i].status == RELOC_TRAMP_FREE ||
                            trampoline_data[i].status == RELOC_TRAMP_IMPORT_DONE) {
                            freeSlot = &(trampoline_data[i]);
                            break;
                        }
                    }
                    if (freeSlot == nullptr) {
                        DEBUG_FUNCTION_LINE_ERR("***24-bit relative branch cannot hit target. Trampoline data list is full");
                        DEBUG_FUNCTION_LINE_ERR("***value %08X - target %08X = distance %08X", value, target, target - (uint32_t) & (freeSlot->trampoline[0]));
                        return false;
                    }
                    auto symbolValue = (uint32_t) & (freeSlot->trampoline[0]);
                    auto newValue    = symbolValue + addend;
                    auto newDistance = static_cast<int32_t>(newValue) - static_cast<int32_t>(target);
                    if (newDistance > 0x1FFFFFC || newDistance < -0x1FFFFFC) {
                        DEBUG_FUNCTION_LINE_ERR("**Cannot link 24-bit jump (too far to tramp buffer).");
                        if (newDistance < 0) {
                            DEBUG_FUNCTION_LINE_ERR("***value %08X - target %08X = distance -%08X", newValue, target, abs(newDistance));
                        } else {
                            DEBUG_FUNCTION_LINE_ERR("***value %08X - target %08X = distance  %08X", newValue, target, newDistance);
                        }
                        return false;
                    }

                    freeSlot->trampoline[0] = 0x3D600000 | ((((uint32_t) value) >> 16) & 0x0000FFFF); // lis r11, real_addr@h
                    freeSlot->trampoline[1] = 0x616B0000 | (((uint32_t) value) & 0x0000ffff);         // ori r11, r11, real_addr@l
                    freeSlot->trampoline[2] = 0x7D6903A6;                                             // mtctr   r11
                    freeSlot->trampoline[3] = 0x4E800420;                                             // bctr
                    DCFlushRange((void *) freeSlot->trampoline, sizeof(freeSlot->trampoline));
                    ICInvalidateRange((unsigned char *) freeSlot->trampoline, sizeof(freeSlot->trampoline));

                    if (reloc_type == RELOC_TYPE_FIXED) {
                        freeSlot->status = RELOC_TRAMP_FIXED;
                    } else {
                        // Relocations for the imports may be overridden
                        freeSlot->status = RELOC_TRAMP_IMPORT_DONE;
                    }
                    distance = newDistance;
                }
            }

            if (distance & 3) {
                DEBUG_FUNCTION_LINE_ERR("***RELOC ERROR %d: lower 2 bits must be zero before shifting.", -470022);
                return false;
            }

            if (distance < 0 && (distance & 0xFE000000) != 0xFE000000) {
                DEBUG_FUNCTION_LINE_ERR("***RELOC ERROR %d: upper 7 bits before shift must all be the same (1).", -470040);
                return false;
            }

            if (distance >= 0 && (distance & 0xFE000000)) {
                DEBUG_FUNCTION_LINE_ERR("***RELOC ERROR %d: upper 7 bits before shift must all be the same (0).", -470040);
                return false;
            }

            *(int32_t *) target = (*(int32_t *) target & 0xfc000003) | (distance & 0x03fffffc);
            break;
        }
        default:
            DEBUG_FUNCTION_LINE_ERR("***ERROR: Unsupported Relocation_Add Type (%08X):", type);
            return false;
    }
    ICInvalidateRange(reinterpret_cast<void *>(target), 4);
    DCFlushRange(reinterpret_cast<void *>(target), 4);
    return true;
}