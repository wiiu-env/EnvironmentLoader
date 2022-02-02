#include <cstring>

#include "utils/StringTools.h"
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
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

// clang-format off
#define MEMORY_REGION_START         0x00A00000
#define MEMORY_REGION_SIZE          0x00600000
#define MEMORY_REGION_END           (MEMORY_REGION_START + MEMORY_REGION_SIZE)

#define AUTOBOOT_CONFIG_PATH        "fs:/vol/external01/wiiu/environments/default.cfg"
// clang-format on

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
    DEBUG_FUNCTION_LINE("Read %s", path.c_str());
    FILE *f = fopen(path.c_str(), "r");
    if (f) {
        char buf[128]{};
        fgets(buf, sizeof(buf), f);
        fclose(f);

        return std::string(buf);
    }
    DEBUG_FUNCTION_LINE("Failed");
    return {};
}

bool writeFileContent(const std::string &path, const std::string &content) {
    DEBUG_FUNCTION_LINE("Write to file %s: %s", path.c_str(), content.c_str());
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fputs(content.c_str(), f);
        fclose(f);
        return true;
    }
    return false;
}

extern "C" void __fini();


int main(int argc, char **argv) {
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

    char environmentPath[0x100];
    memset(environmentPath, 0, sizeof(environmentPath));

    auto handle = IOS_Open("/dev/mcp", IOS_OPEN_READ);
    if (handle >= 0) {
        int in = 0xF9;// IPC_CUSTOM_COPY_ENVIRONMENT_PATH
        if (IOS_Ioctl(handle, 100, &in, sizeof(in), environmentPath, sizeof(environmentPath)) == IOS_ERROR_OK) {
            DEBUG_FUNCTION_LINE("Boot into %s", environmentPath);
        }

        IOS_Close(handle);
    }

    // We substract 0x100 to be safe.
    uint32_t textSectionStart = textStart() - 0x100;

    auto gModuleData = (module_information_t *) (textSectionStart - sizeof(module_information_t));

    bool noEnvironmentsFound = false;

    std::string environment_path = std::string(environmentPath);
    if (strncmp(environmentPath, "fs:/vol/external01/wiiu/environments/", strlen("fs:/vol/external01/wiiu/environments/")) != 0) {
        DirList environmentDirs("fs:/vol/external01/wiiu/environments/", nullptr, DirList::Dirs, 1);

        bool forceMenu = true;
        auto res = getFileContent(AUTOBOOT_CONFIG_PATH);
        auto autobootIndex = -1;
        if (res) {
            DEBUG_FUNCTION_LINE("Got result %s", res->c_str());
            for (int i = 0; i < environmentDirs.GetFilecount(); i++) {
                if (environmentDirs.GetFilename(i) == res.value()) {
                    DEBUG_FUNCTION_LINE("Found environment %s from config at index %d", res.value().c_str(), i);
                    autobootIndex = i;
                    environment_path = environmentDirs.GetFilepath(i);
                    forceMenu = false;
                    break;
                }
            }
        } else {
            DEBUG_FUNCTION_LINE("No config found");
        }

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

        if (forceMenu || (btn & VPAD_BUTTON_X) == VPAD_BUTTON_X) {
            DEBUG_FUNCTION_LINE("Open menu!");
            environment_path = EnvironmentSelectionScreen(environmentPaths, autobootIndex);
            if (environmentPaths.empty()) {
                noEnvironmentsFound = true;
            } else {
                DEBUG_FUNCTION_LINE("Selected %s", environment_path.c_str());
            }
        }
    }
    RevertMainHook();

    if (!noEnvironmentsFound) {
        DirList setupModules(environment_path + "/modules/setup", ".rpx", DirList::Files, 1);
        setupModules.SortList();

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
            // clang-format off
            ((int(*)(int, char **)) moduleData.value()->getEntrypoint())(1, arr);
            // clang-format on
            DEBUG_FUNCTION_LINE("Back from module");
        }
    } else {
        DEBUG_FUNCTION_LINE("Return to Wii U Menu");
        ProcUIInit(OSSavesDone_ReadyToRelease);
        for (int i = 0; i < argc; i++) {
            if (strcmp(argv[i], "void forceDefaultTitleIDToWiiUMenu(void)") == 0) {
                if ((i + 1) < argc) {
                    i++;
                    DEBUG_FUNCTION_LINE("call forceDefaultTitleIDToWiiUMenu");
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

// clang-format off
#define COLOR_WHITE      Color(0xffffffff)
#define COLOR_BLACK      Color(0, 0, 0, 255)
#define COLOR_RED        Color(237, 28, 36, 255)
#define COLOR_BACKGROUND Color(0, 40, 100, 255)
#define COLOR_TEXT       COLOR_WHITE
#define COLOR_TEXT2      Color(0xB3ffffff)
#define COLOR_AUTOBOOT   Color(0xaeea00ff)
#define COLOR_BORDER     Color(204, 204, 204, 255)
#define COLOR_BORDER_HIGHLIGHTED Color(0x3478e4ff)
// clang-format on

std::string EnvironmentSelectionScreen(const std::map<std::string, std::string> &payloads, int32_t autobootIndex) {
    OSScreenInit();

    uint32_t tvBufferSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t drcBufferSize = OSScreenGetBufferSizeEx(SCREEN_DRC);

    auto *screenBuffer = (uint8_t *) memalign(0x100, tvBufferSize + drcBufferSize);
    if (!screenBuffer) {
        OSFatal("Fail to allocate screenBuffer");
    }
    memset(screenBuffer, 0, tvBufferSize + drcBufferSize);

    OSScreenSetBufferEx(SCREEN_TV, screenBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, screenBuffer + tvBufferSize);

    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);

    DrawUtils::initBuffers(screenBuffer, tvBufferSize, screenBuffer + tvBufferSize, drcBufferSize);
    DrawUtils::initFont();

    uint32_t selected = autobootIndex > 0 ? autobootIndex : 0;
    int autoBoot = autobootIndex;

    bool redraw = true;
    while (true) {
        VPADStatus vpad{};
        VPADRead(VPAD_CHAN_0, &vpad, 1, nullptr);

        if (vpad.trigger & VPAD_BUTTON_UP) {
            if (selected > 0) {
                selected--;
                redraw = true;
            }
        } else if (vpad.trigger & VPAD_BUTTON_DOWN) {
            if (selected < payloads.size() - 1) {
                selected++;
                redraw = true;
            }
        } else if (vpad.trigger & VPAD_BUTTON_A) {
            break;
        } else if (vpad.trigger & VPAD_BUTTON_X) {
            autoBoot = -1;
            redraw = true;
        } else if (vpad.trigger & VPAD_BUTTON_Y) {
            autoBoot = selected;
            redraw = true;
        }

        if (redraw) {
            DrawUtils::beginDraw();
            DrawUtils::clear(COLOR_BACKGROUND);

            // draw buttons
            uint32_t index = 8 + 24 + 8 + 4;
            uint32_t i = 0;
            if (!payloads.empty()) {
                for (auto const &[key, val] : payloads) {
                    if (i == selected) {
                        DrawUtils::drawRect(16, index, SCREEN_WIDTH - 16 * 2, 44, 4, COLOR_BORDER_HIGHLIGHTED);
                    } else {
                        DrawUtils::drawRect(16, index, SCREEN_WIDTH - 16 * 2, 44, 2, (i == autoBoot) ? COLOR_AUTOBOOT : COLOR_BORDER);
                    }

                    DrawUtils::setFontSize(24);
                    DrawUtils::setFontColor((i == autoBoot) ? COLOR_AUTOBOOT : COLOR_TEXT);
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

            // draw bottom bar
            DrawUtils::drawRectFilled(8, SCREEN_HEIGHT - 24 - 8 - 4, SCREEN_WIDTH - 8 * 2, 3, COLOR_WHITE);
            DrawUtils::setFontSize(18);
            if (!payloads.empty()) {
                DrawUtils::print(16, SCREEN_HEIGHT - 8, "\ue07d Navigate ");
                DrawUtils::print(SCREEN_WIDTH - 16, SCREEN_HEIGHT - 8, "\ue000 Choose", true);
                const char *autobootHints = "\ue002 Clear Default / \ue003 Select Default";
                DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(autobootHints) / 2, SCREEN_HEIGHT - 8, autobootHints, true);
            } else {
                DrawUtils::print(SCREEN_WIDTH - 20, SCREEN_HEIGHT - 8, "\ue000 Wii U Menu", true);
            }

            DrawUtils::endDraw();

            redraw = false;
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

    int i = 0;
    for (auto const &[key, val] : payloads) {
        if (i == selected) {
            return val;
        }
        i++;
    }

    return "";
}