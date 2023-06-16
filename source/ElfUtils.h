#pragma once

#include "common/relocation_defines.h"
#include "module/RelocationData.h"
#include <coreinit/dynload.h>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

#define R_PPC_NONE           0
#define R_PPC_ADDR32         1
#define R_PPC_ADDR16_LO      4
#define R_PPC_ADDR16_HI      5
#define R_PPC_ADDR16_HA      6
#define R_PPC_REL24          10
#define R_PPC_REL14          11
#define R_PPC_DTPMOD32       68
#define R_PPC_DTPREL32       78
#define R_PPC_EMB_SDA21      109
#define R_PPC_EMB_RELSDA     116
#define R_PPC_DIAB_SDA21_LO  180
#define R_PPC_DIAB_SDA21_HI  181
#define R_PPC_DIAB_SDA21_HA  182
#define R_PPC_DIAB_RELSDA_LO 183
#define R_PPC_DIAB_RELSDA_HI 184
#define R_PPC_DIAB_RELSDA_HA 185
#define R_PPC_GHS_REL16_HA   251
#define R_PPC_GHS_REL16_HI   252
#define R_PPC_GHS_REL16_LO   253

// Masks for manipulating Power PC relocation targets
#define PPC_WORD32           0xFFFFFFFF
#define PPC_WORD30           0xFFFFFFFC
#define PPC_LOW24            0x03FFFFFC
#define PPC_LOW14            0x0020FFFC
#define PPC_HALF16           0xFFFF

#ifdef __cplusplus
}
#endif

class ElfUtils {

public:
    static bool elfLinkOne(char type, size_t offset, int32_t addend, uint32_t destination, uint32_t symbol_addr, relocation_trampoline_entry_t *trampolin_data, uint32_t trampolin_data_length,
                           RelocationType reloc_type);


    static bool doRelocation(const std::vector<std::unique_ptr<RelocationData>> &relocData, relocation_trampoline_entry_t *tramp_data, uint32_t tramp_length, std::map<std::string, OSDynLoad_Module> &usedRPls);
};
