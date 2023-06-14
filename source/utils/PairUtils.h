#pragma once

#include "logger.h"
#include <coreinit/cache.h>
#include <coreinit/im.h>
#include <coreinit/ios.h>
#include <coreinit/time.h>
#include <malloc.h>
#include <nn/ccr/sys.h>

class PairMenu {
public:
    PairMenu();

    ~PairMenu();

    bool ProcessPairScreen();

    static void SyncButtonCallback(IOSError error, void *arg);

    void drawPairScreen() const;

    void drawPairKPADScreen() const;

private:
    enum PairMenuState {
        STATE_WAIT, // Wait for SYNC button press
        STATE_SYNC_WPAD,
        STATE_SYNC_GAMEPAD,
        STATE_PAIRING,
        STATE_CANCEL,
    };

    IOSHandle mIMHandle{};
    IMRequest *mIMRequest{};
    IMRequest *mIMCancelRequest{};
    OSTime mSyncWPADStartTime    = 0;
    OSTime mSyncGamePadStartTime = 0;
    uint32_t mGamePadPincode     = 0;
    PairMenuState mState         = STATE_WAIT;
    uint32_t mGamePadSyncTimeout = 120;
    IMEventMask mIMEventMask{};
};