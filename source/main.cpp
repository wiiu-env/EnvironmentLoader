#include <cstring>

#include "utils/StringTools.h"
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
#include <coreinit/filesystem_fsa.h>
#include <coreinit/foreground.h>
#include <coreinit/ios.h>
#include <coreinit/screen.h>
#include <coreinit/title.h>
#include <elfio/elfio.hpp>
#include <fcntl.h>
#include <gx2/state.h>
#include <malloc.h>
#include <memory>
#include <nn/act/client_cpp.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <sysapp/title.h>
#include <vector>
#include <vpad/input.h>
#include <whb/log_cafe.h>
#include <whb/log_module.h>
#include <whb/log_udp.h>

#include "ElfUtils.h"
#include "common/module_defines.h"
#include "fs/DirList.h"
#include "kernel.h"
#include "module/ModuleDataFactory.h"
#include "utils/DrawUtils.h"
#include "utils/FileUtils.h"
#include "utils/InputUtils.h"
#include "utils/OnLeavingScope.h"
#include "utils/PairUtils.h"
#include "utils/utils.h"
#include "utils/wiiu_zlib.hpp"
#include "version.h"

#define ENVIRONMENT_LOADER_VERSION "v0.2.0"

#define MEMORY_REGION_START        0x00800000
#define AUTOBOOT_CONFIG_PATH       "fs:/vol/external01/wiiu/environments/default.cfg"

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


std::string EnvironmentSelectionScreen(const std::map<std::string, std::string> &payloads, int32_t autobootIndex);

std::optional<std::string> getFileContent(const std::string &path) {
    DEBUG_FUNCTION_LINE_VERBOSE("Read from file %s", path.c_str());
    FILE *f = fopen(path.c_str(), "r");
    if (f) {
        char buf[128]{};
        fgets(buf, sizeof(buf), f);
        fclose(f);

        return std::string(buf);
    }
    DEBUG_FUNCTION_LINE_ERR("Failed to load %s", path.c_str());
    return {};
}

bool writeFileContent(const std::string &path, const std::string &content) {
    DEBUG_FUNCTION_LINE_VERBOSE("Write to file %s: %s", path.c_str(), content.c_str());
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fputs(content.c_str(), f);
        fclose(f);
        return true;
    }
    return false;
}

extern "C" void __fini();
extern "C" void __init_wut_malloc();
void LoadAndRunModule(std::string_view filepath, std::string_view environment_path);

int main(int argc, char **argv) {
    // We need to call __init_wut_malloc somewhere so wut_malloc will be used for the memory allocation.
    __init_wut_malloc();
    initLogging();

    if (IOS_Open((char *) ("/dev/iosuhax"), static_cast<IOSOpenMode>(0)) >= 0) {
        auto checkTiramisuHBL = fopen("fs:/vol/external01/wiiu/environments/tiramisu/modules/setup/50_hbl_installer.rpx", "r");
        if (checkTiramisuHBL != nullptr) {
            fclose(checkTiramisuHBL);
            OSFatal("Don't run the EnvironmentLoader twice.\n\nIf you want to open the Homebrew Launcher, launch the Mii Maker\ninstead.");
        } else {
            OSFatal("Don't run the EnvironmentLoader twice.");
        }
    }

    DEBUG_FUNCTION_LINE("Hello from EnvironmentLoader!");

    char environmentPathFromIOSU[0x100] = {};
    auto handle                         = IOS_Open("/dev/mcp", IOS_OPEN_READ);
    if (handle >= 0) {
        int in = 0xF9; // IPC_CUSTOM_COPY_ENVIRONMENT_PATH
        if (IOS_Ioctl(handle, 100, &in, sizeof(in), environmentPathFromIOSU, sizeof(environmentPathFromIOSU)) == IOS_ERROR_OK) {
            DEBUG_FUNCTION_LINE("Boot into %s", environmentPathFromIOSU);
        }

        IOS_Close(handle);
    }

    bool noEnvironmentsFound = false;

    std::string environmentPath = std::string(environmentPathFromIOSU);
    if (!environmentPath.starts_with("fs:/vol/external01/wiiu/environments/")) { // If the environment path in IOSU is empty or unexpected, read config
        DirList environmentDirs("fs:/vol/external01/wiiu/environments/", nullptr, DirList::Dirs, 1);

        std::map<std::string, std::string> environmentPaths;
        for (int i = 0; i < environmentDirs.GetFilecount(); i++) {
            environmentPaths[environmentDirs.GetFilename(i)] = environmentDirs.GetFilepath(i);
        }

        bool forceMenu     = true;
        auto res           = getFileContent(AUTOBOOT_CONFIG_PATH);
        auto autobootIndex = -1;
        if (res) {
            DEBUG_FUNCTION_LINE_VERBOSE("Got result %s", res->c_str());
            int32_t i = 0;
            for (auto const &[key, val] : environmentPaths) {
                if (res.value() == key) {
                    DEBUG_FUNCTION_LINE("Found environment %s from config at index %d", res.value().c_str(), i);
                    autobootIndex   = i;
                    environmentPath = val;
                    forceMenu       = false;
                    break;
                }
                i++;
            }
        } else {
            DEBUG_FUNCTION_LINE_ERR("No config found");
        }

        InputUtils::Init();

        InputUtils::InputData input = InputUtils::getControllerInput();

        if (forceMenu || ((input.trigger | input.hold) & VPAD_BUTTON_X) == VPAD_BUTTON_X) {
            DEBUG_FUNCTION_LINE_VERBOSE("Open menu!");
            environmentPath = EnvironmentSelectionScreen(environmentPaths, autobootIndex);
            if (environmentPaths.empty()) {
                noEnvironmentsFound = true;
            } else {
                DEBUG_FUNCTION_LINE_VERBOSE("Selected %s", environmentPath.c_str());
            }
        }
        InputUtils::DeInit();
    }
    RevertMainHook();

    if (!noEnvironmentsFound) {
        DirList setupModules(environmentPath + "/modules/setup", ".rpx", DirList::Files, 1);
        setupModules.SortList();

        for (int i = 0; i < setupModules.GetFilecount(); i++) {
            //! skip hidden linux and mac files
            if (setupModules.GetFilename(i)[0] == '.' || setupModules.GetFilename(i)[0] == '_') {
                DEBUG_FUNCTION_LINE_ERR("Skip file %s", setupModules.GetFilepath(i));
                continue;
            }

            LoadAndRunModule(setupModules.GetFilepath(i), environmentPath);
        }

    } else {
        DEBUG_FUNCTION_LINE("Return to Wii U Menu");
        ProcUIInit(OSSavesDone_ReadyToRelease);
        for (int i = 0; i < argc; i++) {
            if (strcmp(argv[i], "void forceDefaultTitleIDToWiiUMenu(void)") == 0) {
                if ((i + 1) < argc) {
                    i++;
                    DEBUG_FUNCTION_LINE_VERBOSE("call forceDefaultTitleIDToWiiUMenu");
                    // clang-format off
                    auto forceDefaultTitleIDToWiiUMenu = (void(*)()) argv[i];
                    // clang-format on
                    forceDefaultTitleIDToWiiUMenu();
                }
            }
        }
        DEBUG_FUNCTION_LINE("Launch menu");
        SYSLaunchMenu();
    }

    ProcUIInit(OSSavesDone_ReadyToRelease);

    while (CheckRunning()) {
        // wait.
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
    ProcUIShutdown();

    deinitLogging();
    __fini();
    return 0;
}

std::optional<HeapWrapper> GetHeapFromMappedMemory(uint32_t heapSize) {
    void *(*MEMAllocFromDefaultHeapExForThreads)(uint32_t size, int align) = nullptr;
    void (*MEMFreeToDefaultHeapForThreads)(void *ptr)                      = nullptr;

    // Let's try to get the memalign and free functions from the memorymapping module.
    OSDynLoad_Module module;
    if (OSDynLoad_Acquire("homebrew_memorymapping", &module) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE("Failed to acquire homebrew_memorymapping.");
        return {};
    }
    /* Memory allocation functions */
    uint32_t *allocPtr = nullptr, *freePtr = nullptr;
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_DATA, "MEMAllocFromMappedMemoryEx", reinterpret_cast<void **>(&allocPtr)) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE("OSDynLoad_FindExport for MEMAllocFromDefaultHeapEx failed");
        return {};
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_DATA, "MEMFreeToMappedMemory", reinterpret_cast<void **>(&freePtr)) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE("OSDynLoad_FindExport for MEMFreeToDefaultHeap failed");
        return {};
    }

    MEMAllocFromDefaultHeapExForThreads = (void *(*) (uint32_t, int) ) * allocPtr;
    MEMFreeToDefaultHeapForThreads      = (void (*)(void *)) * freePtr;

    if (!MEMAllocFromDefaultHeapExForThreads || !MEMFreeToDefaultHeapForThreads) {
        DEBUG_FUNCTION_LINE_ERR("MEMAllocFromDefaultHeapExForThreads or MEMFreeToDefaultHeapForThreads is null");
        // the mapped memory is not available (yet)
        return {};
    }

    uint32_t size = heapSize;
    auto ptr      = MEMAllocFromDefaultHeapExForThreads(size, 0x4);
    if (!ptr) {
        DEBUG_FUNCTION_LINE_ERR("Failed to alloc memory: %d bytes", size);
        return {};
    }

    DEBUG_FUNCTION_LINE("Let's create a memory wrapper for 0x%08X, size: %d", ptr, size);
    return HeapWrapper(MemoryWrapper(ptr, size, MEMFreeToDefaultHeapForThreads));
}

std::optional<HeapWrapper> GetHeapForModule(uint32_t heapSize) {
    // If Aroma is already loaded, we can't use the region between MEMORY_REGION_START and MEMORY_REGION_END anymore because Aroma is using.
    // So instead we check before loading a module if aromas memory mapping module is already usable. If yes, we use this to load the module instead
    if (auto heapWrapper = GetHeapFromMappedMemory(heapSize)) {
        return heapWrapper;
    }

    // If Aroma is not already loaded, we use the existing 0x00800000 - 0x01000000 memory region. This is where aroma is loaded to. Note: this region may be only mapped to the main core.
    // The environment loader is loaded to the end of 0x00800000 - 0x01000000 memory region. With this helper we know the start of the .text section
    uint32_t textSectionStart = textStart() - 0x100;

    auto endOfUsableMemory = textSectionStart;
    uint32_t startAddress  = ((uint32_t) endOfUsableMemory - heapSize) & 0xFFFF0000;
    uint32_t size          = endOfUsableMemory - startAddress;

    if (startAddress < MEMORY_REGION_START) {
        DEBUG_FUNCTION_LINE_ERR("Not enough static memory");
        return {};
    }

    DEBUG_FUNCTION_LINE("Let's create a memory wrapper for 0x%08X, size: %d", ptr, size);
    auto res = HeapWrapper(MemoryWrapper((void *) startAddress, size, /* we don't need to free this memory*/ nullptr));
    if ((uint32_t) res.GetHeapHandle() != startAddress) {
        OSFatal("EnvironmentLoader: Unexpected address");
    }
    return res;
}

void SetupKernelModule() {
    void *(*KernelSetupDefaultSyscalls)() = nullptr;

    OSDynLoad_Module module;
    if (OSDynLoad_Acquire("homebrew_kernel", &module) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE("Failed to acquire homebrew_kernel.");
        return;
    }

    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "KernelSetupDefaultSyscalls", reinterpret_cast<void **>(&KernelSetupDefaultSyscalls)) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE("OSDynLoad_FindExport for KernelSetupDefaultSyscalls failed");
        OSFatal("EnvironmentLoader: KernelModule is missing the export\n"
                "\"KernelSetupDefaultSyscalls\"... Please update Aroma!\n"
                "\n"
                "See https://wiiu.hacks.guide/ for more information.");
        return;
    }

    if (!KernelSetupDefaultSyscalls) {
        DEBUG_FUNCTION_LINE_WARN("KernelSetupDefaultSyscalls is null");
        OSFatal("EnvironmentLoader: KernelModule is missing the export\n"
                "\"KernelSetupDefaultSyscalls\"... Please update Aroma!\n"
                "\n"
                "See https://wiiu.hacks.guide/ for more information.");
        return;
    }

    DEBUG_FUNCTION_LINE("Call KernelSetupDefaultSyscalls");
    KernelSetupDefaultSyscalls();
    OSDynLoad_Release(module);
}

void LoadAndRunModule(std::string_view filepath, std::string_view environment_path) {
    // Some module may unmount the sd card on exit.
    FSAInit();
    auto client = FSAAddClient(nullptr);
    if (client) {
        FSAMount(client, "/dev/sdcard01", "/vol/external01", static_cast<FSAMountFlags>(0), nullptr, 0);
        FSADelClient(client);
    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to add FSA client");
    }

    DEBUG_FUNCTION_LINE("Trying to load %s into memory", filepath.data());
    uint8_t *buffer = nullptr;
    uint32_t fsize  = 0;
    if (LoadFileToMem(filepath.data(), &buffer, &fsize) < 0) {
        DEBUG_FUNCTION_LINE_ERR("Failed to load file");
        OSFatal("EnvironmentLoader: Failed to load file to memory");
        return;
    }

    auto cleanupBuffer = onLeavingScope([buffer]() { free(buffer); });

    ELFIO::elfio reader(new wiiu_zlib);
    // Load ELF data
    if (!reader.load(reinterpret_cast<const char *>(buffer), fsize)) {
        DEBUG_FUNCTION_LINE_ERR("Can't parse .wms from buffer.");
        OSFatal("Can't parse .wms from buffer.");
        return;
    }

    uint32_t moduleSize = ModuleDataFactory::GetSizeOfModule(reader);
    DEBUG_FUNCTION_LINE_VERBOSE("Module has size: %d", moduleSize);

    uint32_t requiredHeapSize = moduleSize + sizeof(module_information_t) + 0x10000; // add another 0x10000 to be safe
    DEBUG_FUNCTION_LINE_VERBOSE("Allocate %d bytes for heap (%.2f KiB)", requiredHeapSize, requiredHeapSize / 1024.0f);

    if (auto heapWrapperOpt = GetHeapForModule(requiredHeapSize); heapWrapperOpt.has_value()) {
        // Frees automatically, must not survive the heapWrapper.
        auto moduleInfoOpt = heapWrapperOpt->Alloc(sizeof(module_information_t), 0x4);
        if (!moduleInfoOpt) {
            DEBUG_FUNCTION_LINE_ERR("Failed to alloc module information");
            OSFatal("EnvironmentLoader: Failed to alloc module information");
            return;
        }

        auto moduleInfo    = std::move(*moduleInfoOpt);
        auto moduleInfoPtr = (module_information_t *) moduleInfo.data();

        // Frees automatically, must not survive the heapWrapper.
        auto moduleData = ModuleDataFactory::load(reader, *heapWrapperOpt, moduleInfoPtr->trampolines, sizeof(moduleInfoPtr->trampolines) / sizeof(moduleInfoPtr->trampolines[0]));
        if (!moduleData) {
            DEBUG_FUNCTION_LINE_ERR("Failed to load %s", filepath);
            OSFatal("EnvironmentLoader: Failed to load module");
            return;
        }

        DEBUG_FUNCTION_LINE("Loaded module data");
        std::map<std::string, OSDynLoad_Module> usedRPls;
        if (!ElfUtils::doRelocation(moduleData.value()->getRelocationDataList(), moduleInfoPtr->trampolines, sizeof(moduleInfoPtr->trampolines) / sizeof(moduleInfoPtr->trampolines[0]), usedRPls)) {
            DEBUG_FUNCTION_LINE_ERR("Relocations failed");
            OSFatal("EnvironmentLoader: Relocations failed");
        } else {
            DEBUG_FUNCTION_LINE("Relocation done");
        }

        DCFlushRange((void *) moduleData.value()->getStartAddress(), moduleData.value()->getEndAddress() - moduleData.value()->getStartAddress());
        ICInvalidateRange((void *) moduleData.value()->getStartAddress(), moduleData.value()->getEndAddress() - moduleData.value()->getStartAddress());

        char *arr[4];
        arr[0] = (char *) environment_path.data();
        arr[1] = (char *) "EnvironmentLoader"; //
        arr[2] = (char *) 0x02;                // Version
        /*
         * This is a hacky work around to tell Aromas Module Loader which memory region it can use safely. After using it, it's expected to expose new memory region via the
         * custom rpl "homebrew_mappedmemory" (See: GetHeapFromMappedMemory). The returned memory is expected to be RWX for user and kernel.
         * Once a custom memory allocator is provided, usable_mem_start and usable_mem_end are set to 0.
         */
        auto usable_mem_end = (uint32_t) heapWrapperOpt->GetHeapHandle();
        if (heapWrapperOpt->IsAllocated()) { // Check if you use memory which is actually allocated. This means we can't give it to the module.
            DEBUG_FUNCTION_LINE("Don't give the module a usable memory region because it will be loaded on a custom memory region.");
            usable_mem_end = 0;
        }
        arr[3] = (char *) usable_mem_end; // End of usable memory

        DEBUG_FUNCTION_LINE("Calling entrypoint @%08X with: \"%s\", \"%s\", %08X, %08X", moduleData.value()->getEntrypoint(), arr[0], arr[1], arr[2], arr[3]);
        // clang-format off
        ((int(*)(int, char **)) moduleData.value()->getEntrypoint())(sizeof(arr)/ sizeof(arr[0]), arr);
        // clang-format on
        DEBUG_FUNCTION_LINE("Back from module");

        for (auto &rpl : usedRPls) {
            DEBUG_FUNCTION_LINE_VERBOSE("Release %s", rpl.first.c_str());
            OSDynLoad_Release(rpl.second);
        }

    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to create heap");
        OSFatal("EnvironmentLoader: Failed to create heap");
    }

    // module may override the syscalls used by the Aroma KernelModule. This (tries to) re-init(s) the KernelModule after a setup module has been run.
    SetupKernelModule();
}

std::string EnvironmentSelectionScreen(const std::map<std::string, std::string> &payloads, int32_t autobootIndex) {
    OSScreenInit();

    uint32_t tvBufferSize  = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t drcBufferSize = OSScreenGetBufferSizeEx(SCREEN_DRC);

    auto *screenBuffer = (uint8_t *) memalign(0x100, tvBufferSize + drcBufferSize);
    if (!screenBuffer) {
        OSFatal("EnvironmentLoader: Fail to allocate screenBuffer");
    }
    memset(screenBuffer, 0, tvBufferSize + drcBufferSize);

    OSScreenSetBufferEx(SCREEN_TV, screenBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, screenBuffer + tvBufferSize);

    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);

    DrawUtils::initBuffers(screenBuffer, tvBufferSize, screenBuffer + tvBufferSize, drcBufferSize);

    if (!DrawUtils::initFont()) {
        OSFatal("EnvironmentLoader: Failed to init font");
    }

    uint32_t selected = autobootIndex > 0 ? autobootIndex : 0;
    int autoBoot      = autobootIndex;

    {
        PairMenu pairMenu;
        while (true) {
            if (pairMenu.ProcessPairScreen()) {
                continue;
            }

            InputUtils::InputData input = InputUtils::getControllerInput();

            if (input.trigger & VPAD_BUTTON_UP) {
                if (selected > 0) {
                    selected--;
                }
            } else if (input.trigger & VPAD_BUTTON_DOWN) {
                if (selected < payloads.size() - 1) {
                    selected++;
                }
            } else if (input.trigger & VPAD_BUTTON_A) {
                break;
            } else if (input.trigger & (VPAD_BUTTON_X | VPAD_BUTTON_MINUS)) {
                autoBoot = -1;
            } else if (input.trigger & (VPAD_BUTTON_Y | VPAD_BUTTON_PLUS)) {
                autoBoot = selected;
            }


            DrawUtils::beginDraw();
            DrawUtils::clear(COLOR_BACKGROUND);

            // draw buttons
            uint32_t index = 8 + 24 + 8 + 4;
            uint32_t i     = 0;
            if (!payloads.empty()) {
                for (auto const &[key, val] : payloads) {
                    if (i == selected) {
                        DrawUtils::drawRect(16, index, SCREEN_WIDTH - 16 * 2, 44, 4, COLOR_BORDER_HIGHLIGHTED);
                    } else {
                        DrawUtils::drawRect(16, index, SCREEN_WIDTH - 16 * 2, 44, 2, ((int32_t) i == autoBoot) ? COLOR_AUTOBOOT : COLOR_BORDER);
                    }

                    DrawUtils::setFontSize(24);
                    DrawUtils::setFontColor(((int32_t) i == autoBoot) ? COLOR_AUTOBOOT : COLOR_TEXT);
                    DrawUtils::print(16 * 2, index + 8 + 24, key.c_str());
                    index += 42 + 8;
                    i++;
                }
            } else {
                DrawUtils::setFontSize(24);
                DrawUtils::setFontColor(COLOR_RED);
                const char *noEnvironmentsWarning = "No valid environments found. Press \ue000 to launch the Wii U Menu";
                DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(noEnvironmentsWarning) / 2, SCREEN_HEIGHT / 2, noEnvironmentsWarning, true);
            }

            DrawUtils::setFontColor(COLOR_TEXT);

            // draw top bar
            DrawUtils::setFontSize(24);
            DrawUtils::print(16, 6 + 24, "Environment Loader");
            DrawUtils::drawRectFilled(8, 8 + 24 + 4, SCREEN_WIDTH - 8 * 2, 3, COLOR_WHITE);
            DrawUtils::setFontSize(16);
            DrawUtils::print(SCREEN_WIDTH - 16, 6 + 24, ENVIRONMENT_LOADER_VERSION ENVIRONMENT_LOADER_VERSION_EXTRA, true);

            // draw bottom bar
            DrawUtils::drawRectFilled(8, SCREEN_HEIGHT - 24 - 8 - 4, SCREEN_WIDTH - 8 * 2, 3, COLOR_WHITE);
            DrawUtils::setFontSize(18);
            if (!payloads.empty()) {
                DrawUtils::print(16, SCREEN_HEIGHT - 8, "\ue07d Navigate ");
                DrawUtils::print(SCREEN_WIDTH - 16, SCREEN_HEIGHT - 8, "\ue000 Choose", true);
                const char *autobootHints = "\ue002/\ue046 Clear Default / \ue003/\ue045 Select Default";
                DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(autobootHints) / 2, SCREEN_HEIGHT - 8, autobootHints, true);
            } else {
                DrawUtils::print(SCREEN_WIDTH - 20, SCREEN_HEIGHT - 8, "\ue000 Wii U Menu", true);
            }

            DrawUtils::endDraw();
        }
    }

    DrawUtils::beginDraw();
    DrawUtils::clear(COLOR_BLACK);
    DrawUtils::endDraw();

    DrawUtils::deinitFont();

    // Call GX2Init to shut down OSScreen
    GX2Init(nullptr);

    free(screenBuffer);

    if (autoBoot != autobootIndex) {
        if (autoBoot == -1) {
            writeFileContent(AUTOBOOT_CONFIG_PATH, "-1");
        } else {
            int i = 0;
            for (auto const &[key, val] : payloads) {
                if (i == autoBoot) {
                    DEBUG_FUNCTION_LINE("Save config");
                    writeFileContent(AUTOBOOT_CONFIG_PATH, key);
                    break;
                }
                i++;
            }
        }
    }

    uint32_t i = 0;
    for (auto const &[key, val] : payloads) {
        if (i == selected) {
            return val;
        }
        i++;
    }

    return "";
}