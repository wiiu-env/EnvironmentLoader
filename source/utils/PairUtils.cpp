#include "PairUtils.h"
#include "DrawUtils.h"
#include "InputUtils.h"
#include "logger.h"
#include "utils.h"
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <malloc.h>
#include <nn/ccr/sys.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

void PairMenu::drawPairKPADScreen() const {
    DrawUtils::beginDraw();
    DrawUtils::clear(COLOR_BACKGROUND);

    DrawUtils::setFontColor(COLOR_TEXT);

    DrawUtils::setFontSize(26);

    std::string textLine1 = "Press the SYNC Button on the controller you want to pair.";
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(textLine1.c_str()) / 2, 40, textLine1.c_str(), true);


    WPADExtensionType ext{};
    for (int i = 0; i < 4; i++) {
        bool isConnected     = WPADProbe((WPADChan) i, &ext) == 0;
        std::string textLine = string_format("Slot %d: ", i + 1);
        if (isConnected) {
            textLine += ext == WPAD_EXT_PRO_CONTROLLER ? "Pro Controller" : "Wiimote";
        } else {
            textLine += "No controller";
        }

        DrawUtils::print(300, 140 + (i * 30), textLine.c_str());
    }

    DrawUtils::setFontSize(26);

    std::string gamepadSyncText1 = "If you are pairing a Wii U GamePad, press the SYNC Button";
    std::string gamepadSyncText2 = "on your Wii U console one more time";
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(gamepadSyncText1.c_str()) / 2, SCREEN_HEIGHT - 100, gamepadSyncText1.c_str(), true);
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(gamepadSyncText2.c_str()) / 2, SCREEN_HEIGHT - 70, gamepadSyncText2.c_str(), true);

    DrawUtils::setFontSize(16);

    const char *exitHints = "Press \ue001 to return";
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(exitHints) / 2, SCREEN_HEIGHT - 8, exitHints, true);

    DrawUtils::endDraw();
}

void PairMenu::drawPairScreen() const {
    DrawUtils::beginDraw();
    DrawUtils::clear(COLOR_BACKGROUND);

    DrawUtils::setFontColor(COLOR_TEXT);

    // Convert the pin to symbols and set the text
    static char pinSymbols[][4] = {
            "\u2660",
            "\u2665",
            "\u2666",
            "\u2663"};

    uint32_t pincode = mGamePadPincode;

    std::string pin = std::string(pinSymbols[(pincode / 1000) % 10]) +
                      pinSymbols[(pincode / 100) % 10] +
                      pinSymbols[(pincode / 10) % 10] +
                      pinSymbols[pincode % 10];

    std::string textLine1 = "Press the SYNC Button on the Wii U GamePad,";
    std::string textLine2 = "and enter the four symbols shown below.";

    DrawUtils::setFontSize(26);

    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(textLine1.c_str()) / 2, 60, textLine1.c_str(), true);
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(textLine2.c_str()) / 2, 100, textLine2.c_str(), true);

    DrawUtils::setFontSize(100);
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(pin.c_str()) / 2, (SCREEN_HEIGHT / 2) + 40, pin.c_str(), true);

    DrawUtils::setFontSize(20);

    std::string textLine3 = string_format("(%d seconds remaining) ", mGamePadSyncTimeout - (uint32_t) (OSTicksToSeconds(OSGetTime() - mSyncGamePadStartTime)));
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(textLine3.c_str()) / 2, SCREEN_HEIGHT - 80, textLine3.c_str(), true);

    DrawUtils::setFontSize(26);

    std::string textLine4 = "Press the SYNC Button on the Wii U console to exit.";
    DrawUtils::print(SCREEN_WIDTH / 2 + DrawUtils::getTextWidth(textLine4.c_str()) / 2, SCREEN_HEIGHT - 40, textLine4.c_str(), true);

    DrawUtils::endDraw();
}

PairMenu::PairMenu() {
    CCRSysInit();

    mState              = STATE_WAIT;
    mGamePadSyncTimeout = 120;

    // Initialize IM
    mIMHandle = IM_Open();
    if (mIMHandle < 0) {
        DEBUG_FUNCTION_LINE_ERR("PairMenu: IM_Open failed");
        OSFatal("PairMenu: IM_Open failed");
    }
    mIMRequest = (IMRequest *) memalign(0x40, sizeof(IMRequest));

    // Allocate a separate request for IM_CancelGetEventNotify to avoid conflict with the pending IM_GetEventNotify request
    mIMCancelRequest = (IMRequest *) memalign(0x40, sizeof(IMRequest));

    if (!mIMRequest || !mIMCancelRequest) {
        DEBUG_FUNCTION_LINE_ERR("Failed to allocate im request");
        OSFatal("PairMenu: Failed to allocate im request");
    }

    mIMEventMask = IM_EVENT_SYNC;

    // Notify about sync button events
    IM_GetEventNotify(mIMHandle, mIMRequest, &mIMEventMask, PairMenu::SyncButtonCallback, this);
}

PairMenu::~PairMenu() {
    // Close IM
    IM_CancelGetEventNotify(mIMHandle, mIMCancelRequest, nullptr, nullptr);
    IM_Close(mIMHandle);
    if (mIMCancelRequest) {
        free(mIMCancelRequest);
        mIMCancelRequest = {};
    }
    if (mIMRequest) {
        free(mIMRequest);
        mIMRequest = {};
    }

    // Deinit CCRSys
    CCRSysExit();
}

extern "C" bool WPADStartSyncDevice();

bool PairMenu::ProcessPairScreen() {
    switch (mState) {
        case STATE_SYNC_WPAD: {
            // WPAD syncing stops after ~18 seconds, make sure to restart it.
            if ((uint32_t) OSTicksToSeconds(OSGetTime() - mSyncWPADStartTime) >= 18) {
                WPADStartSyncDevice();
                mSyncWPADStartTime = OSGetTime();
            }

            InputUtils::InputData input = InputUtils::getControllerInput();

            // Stop syncing when pressing A or B.
            if (input.trigger & (VPAD_BUTTON_A | VPAD_BUTTON_B)) {
                mState = STATE_WAIT;
            }

            break;
        }
        case STATE_SYNC_GAMEPAD: {
            if (CCRSysGetPincode(&mGamePadPincode) != 0) {
                DEBUG_FUNCTION_LINE_ERR("CCRSysGetPincode failed");
                mState = STATE_WAIT;
                break;
            }

            // Start pairing to slot 0
            if (CCRSysStartPairing(0, mGamePadSyncTimeout) != 0) {
                DEBUG_FUNCTION_LINE_ERR("CCRSysStartPairing failed.");
                mState = STATE_WAIT;
                break;
            }

            // Pairing has started, save start time
            mSyncGamePadStartTime = OSGetTime();
            mState                = STATE_PAIRING;

            DEBUG_FUNCTION_LINE("Started GamePad syncing.");

            break;
        }
        case STATE_PAIRING: {
            // Get the current pairing state
            CCRSysPairingState pairingState = CCRSysGetPairingState();
            if (pairingState == CCR_SYS_PAIRING_TIMED_OUT) {
                DEBUG_FUNCTION_LINE("GamePad SYNC timed out.");
                // Pairing has timed out or was cancelled
                CCRSysStopPairing();
                mState = STATE_WAIT;
            } else if (pairingState == CCR_SYS_PAIRING_FINISHED) {
                DEBUG_FUNCTION_LINE("GamePad paired.");
                mState = STATE_WAIT;
            }
            break;
        }
        case STATE_CANCEL: {
            CCRSysStopPairing();
            mState = STATE_WAIT;
            break;
        }
        case STATE_WAIT:
            break;
    }
    switch (mState) {
        case STATE_WAIT: {
            return false;
        }
        case STATE_SYNC_WPAD:
            drawPairKPADScreen();
            break;
        case STATE_SYNC_GAMEPAD:
        case STATE_PAIRING:
        case STATE_CANCEL: {
            drawPairScreen();
            break;
        }
    }
    return true;
}


void PairMenu::SyncButtonCallback(IOSError error, void *arg) {
    auto *pairMenu = (PairMenu *) arg;

    if (error == IOS_ERROR_OK && pairMenu && (pairMenu->mIMEventMask & IM_EVENT_SYNC)) {
        if (pairMenu->mState == STATE_WAIT) {
            pairMenu->mState = STATE_SYNC_WPAD;
            // We need to restart the WPAD pairing every 18 seconds. For the timing we need to save the current time.
            pairMenu->mSyncWPADStartTime = OSGetTime();
        } else if (pairMenu->mState == STATE_SYNC_WPAD) {
            pairMenu->mState = STATE_SYNC_GAMEPAD;
        } else if (pairMenu->mState == STATE_SYNC_GAMEPAD || pairMenu->mState == STATE_PAIRING) {
            pairMenu->mState = STATE_CANCEL;
        }
        OSMemoryBarrier();
        IM_GetEventNotify(pairMenu->mIMHandle, pairMenu->mIMRequest, &pairMenu->mIMEventMask, PairMenu::SyncButtonCallback, pairMenu);
    }
}
