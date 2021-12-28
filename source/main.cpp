#include <cstring>

#include <elfio/elfio.hpp>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <coreinit/foreground.h>
#include <coreinit/cache.h>
#include <coreinit/ios.h>
#include <nn/act/client_cpp.h>
#include <coreinit/dynload.h>
#include <whb/log_udp.h>
#include <whb/log_cafe.h>
#include <whb/log_module.h>
#include <vector>
#include <coreinit/dynload.h>
#include <coreinit/screen.h>
#include <memory>
#include <malloc.h>
#include <vpad/input.h>
#include <coreinit/debug.h>
#include "utils/StringTools.h"

#include "fs/DirList.h"
#include "module/ModuleDataFactory.h"
#include "ElfUtils.h"
#include "kernel.h"
#include "common/module_defines.h"

#define MEMORY_REGION_START         0x00900000
#define MEMORY_REGION_SIZE          0x00700000
#define MEMORY_REGION_END           (MEMORY_REGION_START + MEMORY_REGION_SIZE)

bool CheckRunning() {
    switch (ProcUIProcessMessages(true)) {
        case PROCUI_STATUS_EXITING: {
            return false;
        }
        case PROCUI_STATUS_RELEASE_FOREGROUND: {
            ProcUIDrawDoneRelease();
            break;
        }
        case PROCUI_STATUS_IN_FOREGROUND: {
            break;
        }
        case PROCUI_STATUS_IN_BACKGROUND:
        default:
            break;
    }
    return true;
}

extern "C" uint32_t textStart();


std::string EnvironmentSelectionScreen(const std::map<std::string, std::string> &payloads);

int main(int argc, char **argv) {
#ifdef DEBUG
    if (!WHBLogModuleInit()) {
        WHBLogCafeInit();
        WHBLogUdpInit();
    }
#endif // DEBUG

    DEBUG_FUNCTION_LINE("Hello from EnvironmentLoader!");

    char environmentPath[0x100];
    memset(environmentPath, 0, sizeof(environmentPath));

    auto handle = IOS_Open("/dev/mcp", IOS_OPEN_READ);
    if (handle >= 0) {
        int in = 0xF9; // IPC_CUSTOM_COPY_ENVIRONMENT_PATH
        if (IOS_Ioctl(handle, 100, &in, sizeof(in), environmentPath, sizeof(environmentPath)) == IOS_ERROR_OK) {
            DEBUG_FUNCTION_LINE("Boot into %s", environmentPath);
        }

        IOS_Close(handle);
    }

    // We substract 0x100 to be safe.
    uint32_t textSectionStart = textStart() - 0x100;

    auto gModuleData = (module_information_t *) (textSectionStart - sizeof(module_information_t));

    std::string environment_path = std::string(environmentPath);
    if (strncmp(environmentPath, "fs:/vol/external01/wiiu/environments/", strlen("fs:/vol/external01/wiiu/environments/")) != 0) {
        DirList environmentDirs("fs:/vol/external01/wiiu/environments/", nullptr, DirList::Dirs, 1);

        std::map<std::string, std::string> environmentPaths;
        for (int i = 0; i < environmentDirs.GetFilecount(); i++) {
            environmentPaths[environmentDirs.GetFilename(i)] = environmentDirs.GetFilepath(i);
        }

        VPADReadError err;
        VPADStatus vpad_data;
        VPADRead(VPAD_CHAN_0, &vpad_data, 1, &err);

        uint32_t btn = 0;
        if (err == VPAD_READ_SUCCESS) {
            btn = vpad_data.hold | vpad_data.trigger;
        }

        environment_path = "fs:/vol/external01/wiiu/environments/default";

        if ((btn & VPAD_BUTTON_X) == VPAD_BUTTON_X) {
            environment_path = EnvironmentSelectionScreen(environmentPaths);
            DEBUG_FUNCTION_LINE("Selected %s", environment_path.c_str());
        }
    }

    DirList setupModules(environment_path + "/modules/setup", ".rpx", DirList::Files, 1);
    setupModules.SortList();

    RevertMainHook();

    for (int i = 0; i < setupModules.GetFilecount(); i++) {
        uint32_t destination_address_end = ((uint32_t) gModuleData) & 0xFFFF0000;
        memset((void *) gModuleData, 0, sizeof(module_information_t));
        DEBUG_FUNCTION_LINE("Trying to run %s.", setupModules.GetFilepath(i), destination_address_end, ((uint32_t) gModuleData) - MEMORY_REGION_START);
        auto moduleData = ModuleDataFactory::load(setupModules.GetFilepath(i), destination_address_end, ((uint32_t) gModuleData) - MEMORY_REGION_START, gModuleData->trampolines,
                                                  DYN_LINK_TRAMPOLIN_LIST_LENGTH);
        if (!moduleData) {
            DEBUG_FUNCTION_LINE("Failed to load %s", setupModules.GetFilepath(i));
            continue;
        }
        DEBUG_FUNCTION_LINE("Loaded module data");
        auto relocData = moduleData.value()->getRelocationDataList();
        if (!ElfUtils::doRelocation(relocData, gModuleData->trampolines, DYN_LINK_TRAMPOLIN_LIST_LENGTH)) {
            OSFatal("Relocations failed");
        } else {
            DEBUG_FUNCTION_LINE("Relocation done");
        }

        DCFlushRange((void *) moduleData.value()->getStartAddress(), moduleData.value()->getEndAddress() - moduleData.value()->getStartAddress());
        ICInvalidateRange((void *) moduleData.value()->getStartAddress(), moduleData.value()->getEndAddress() - moduleData.value()->getStartAddress());

        DEBUG_FUNCTION_LINE("Calling entrypoint @%08X", moduleData.value()->getEntrypoint());
        char *arr[1];
        arr[0] = (char *) environment_path.c_str();
        ((int (*)(int, char **)) moduleData.value()->getEntrypoint())(1, arr);
        DEBUG_FUNCTION_LINE("Back from module");
    }

    ProcUIInit(OSSavesDone_ReadyToRelease);

    while (CheckRunning()) {
        // wait.
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
    ProcUIShutdown();

    return 0;
}

std::string EnvironmentSelectionScreen(const std::map<std::string, std::string> &payloads) {
    // Init screen and screen buffers
    OSScreenInit();
    uint32_t screen_buf0_size = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t screen_buf1_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
    auto *screenBuffer = (uint8_t *) memalign(0x100, screen_buf0_size + screen_buf1_size);
    OSScreenSetBufferEx(SCREEN_TV, (void *) screenBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, (void *) (screenBuffer + screen_buf0_size));

    OSScreenEnableEx(SCREEN_TV, 1);
    OSScreenEnableEx(SCREEN_DRC, 1);

    // Clear screens
    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);

    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);

    VPADStatus vpad_data;
    VPADReadError error;
    int selected = 0;
    std::string header = "Please choose your environment:";
    while (true) {
        // Clear screens
        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        int pos = 0;

        OSScreenPutFontEx(SCREEN_TV, 0, pos, header.c_str());
        OSScreenPutFontEx(SCREEN_DRC, 0, pos, header.c_str());

        pos += 2;

        int i = 0;
        for (auto const&[key, val]: payloads) {
            std::string text = StringTools::strfmt("%s %s", i == selected ? "> " : "  ", key.c_str());
            OSScreenPutFontEx(SCREEN_TV, 0, pos, text.c_str());
            OSScreenPutFontEx(SCREEN_DRC, 0, pos, text.c_str());
            i++;
            pos++;
        }

        VPADRead(VPAD_CHAN_0, &vpad_data, 1, &error);
        if (vpad_data.trigger == VPAD_BUTTON_A) {
            break;
        }

        if (vpad_data.trigger == VPAD_BUTTON_UP) {
            selected--;
            if (selected < 0) {
                selected = 0;
            }
        } else if (vpad_data.trigger == VPAD_BUTTON_DOWN) {
            selected++;
            if ((uint32_t) selected >= payloads.size()) {
                selected = payloads.size() - 1;
            }
        }

        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);

        OSSleepTicks(OSMillisecondsToTicks(16));
    }
    int i = 0;
    for (auto const&[key, val]: payloads) {
        if (i == selected) {
            free(screenBuffer);
            return val;
        }
        i++;
    }
    free(screenBuffer);
    return "";
}