#pragma once
#include <cstdint>
#include <vpad/input.h>

class InputUtils {
public:
    typedef struct InputData {
        uint32_t trigger = 0;
        uint32_t hold    = 0;
        uint32_t release = 0;
    } InputData;

    static void Init();
    static void DeInit();

    static InputData getControllerInput();
};
