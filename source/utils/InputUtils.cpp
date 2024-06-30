#include "InputUtils.h"
#include <coreinit/thread.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

uint32_t remapWiiMoteButtons(uint32_t buttons) {
    uint32_t convButtons = 0;

    if (buttons & WPAD_BUTTON_LEFT)
        convButtons |= VPAD_BUTTON_LEFT;

    if (buttons & WPAD_BUTTON_RIGHT)
        convButtons |= VPAD_BUTTON_RIGHT;

    if (buttons & WPAD_BUTTON_DOWN)
        convButtons |= VPAD_BUTTON_DOWN;

    if (buttons & WPAD_BUTTON_UP)
        convButtons |= VPAD_BUTTON_UP;

    if (buttons & WPAD_BUTTON_PLUS)
        convButtons |= VPAD_BUTTON_PLUS;

    if (buttons & WPAD_BUTTON_2)
        convButtons |= VPAD_BUTTON_Y;

    if (buttons & WPAD_BUTTON_1)
        convButtons |= VPAD_BUTTON_X;

    if (buttons & WPAD_BUTTON_B)
        convButtons |= VPAD_BUTTON_B;

    if (buttons & WPAD_BUTTON_A)
        convButtons |= VPAD_BUTTON_A;

    if (buttons & WPAD_BUTTON_MINUS)
        convButtons |= VPAD_BUTTON_MINUS;

    if (buttons & WPAD_BUTTON_HOME)
        convButtons |= VPAD_BUTTON_HOME;

    return convButtons;
}

uint32_t remapClassicButtons(uint32_t buttons) {
    uint32_t convButtons = 0;

    if (buttons & WPAD_CLASSIC_BUTTON_LEFT)
        convButtons |= VPAD_BUTTON_LEFT;

    if (buttons & WPAD_CLASSIC_BUTTON_RIGHT)
        convButtons |= VPAD_BUTTON_RIGHT;

    if (buttons & WPAD_CLASSIC_BUTTON_DOWN)
        convButtons |= VPAD_BUTTON_DOWN;

    if (buttons & WPAD_CLASSIC_BUTTON_UP)
        convButtons |= VPAD_BUTTON_UP;

    if (buttons & WPAD_CLASSIC_BUTTON_PLUS)
        convButtons |= VPAD_BUTTON_PLUS;

    if (buttons & WPAD_CLASSIC_BUTTON_X)
        convButtons |= VPAD_BUTTON_X;

    if (buttons & WPAD_CLASSIC_BUTTON_Y)
        convButtons |= VPAD_BUTTON_Y;

    if (buttons & WPAD_CLASSIC_BUTTON_B)
        convButtons |= VPAD_BUTTON_B;

    if (buttons & WPAD_CLASSIC_BUTTON_A)
        convButtons |= VPAD_BUTTON_A;

    if (buttons & WPAD_CLASSIC_BUTTON_MINUS)
        convButtons |= VPAD_BUTTON_MINUS;

    if (buttons & WPAD_CLASSIC_BUTTON_HOME)
        convButtons |= VPAD_BUTTON_HOME;

    if (buttons & WPAD_CLASSIC_BUTTON_ZR)
        convButtons |= VPAD_BUTTON_ZR;

    if (buttons & WPAD_CLASSIC_BUTTON_ZL)
        convButtons |= VPAD_BUTTON_ZL;

    if (buttons & WPAD_CLASSIC_BUTTON_R)
        convButtons |= VPAD_BUTTON_R;

    if (buttons & WPAD_CLASSIC_BUTTON_L)
        convButtons |= VPAD_BUTTON_L;

    return convButtons;
}

InputUtils::InputData InputUtils::getControllerInput() {
    InputData inputData     = {};
    VPADStatus vpadStatus   = {};
    VPADReadError vpadError = VPAD_READ_UNINITIALIZED;
    int maxAttempts         = 100;
    do {
        if (VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError) > 0 && vpadError == VPAD_READ_SUCCESS) {
            inputData.trigger = vpadStatus.trigger;
            inputData.hold    = vpadStatus.hold;
            inputData.release = vpadStatus.release;
        } else {
            OSSleepTicks(OSMillisecondsToTicks(1));
        }
    } while (--maxAttempts > 0 && vpadError == VPAD_READ_NO_SAMPLES);

    KPADStatus kpadStatus = {};
    KPADError kpadError   = KPAD_ERROR_UNINITIALIZED;
    for (int32_t i = 0; i < 4; i++) {
        if (KPADReadEx((KPADChan) i, &kpadStatus, 1, &kpadError) > 0) {
            if (kpadError == KPAD_ERROR_OK && kpadStatus.extensionType != 0xFF) {
                if (kpadStatus.extensionType == WPAD_EXT_CORE || kpadStatus.extensionType == WPAD_EXT_NUNCHUK) {
                    inputData.trigger |= remapWiiMoteButtons(kpadStatus.trigger);
                    inputData.hold |= remapWiiMoteButtons(kpadStatus.hold);
                    inputData.release |= remapWiiMoteButtons(kpadStatus.release);
                } else {
                    inputData.trigger |= remapClassicButtons(kpadStatus.classic.trigger);
                    inputData.hold |= remapClassicButtons(kpadStatus.classic.hold);
                    inputData.release |= remapClassicButtons(kpadStatus.classic.release);
                }
            }
        }
    }

    return inputData;
}

void InputUtils::Init() {
    KPADInit();
    WPADEnableURCC(1);
}

void InputUtils::DeInit() {
    KPADShutdown();
}