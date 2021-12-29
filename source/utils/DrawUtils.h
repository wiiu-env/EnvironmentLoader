#pragma once

#include <cstdint>

// visible screen sizes
#define SCREEN_WIDTH 854
#define SCREEN_HEIGHT 480

union Color {
    Color(uint32_t color) { 
        this->color = color; 
    }
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { 
        this->r = r; this->g = g; this->b = b; this->a = a; 
    }

    uint32_t color;
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
};

class DrawUtils {
public:
    static void initBuffers(void* tvBuffer, uint32_t tvSize, void* drcBuffer, uint32_t drcSize);
    static void beginDraw();
    static void endDraw();
    static void clear(Color col);
    static void drawPixel(uint32_t x, uint32_t y, Color col) { drawPixel(x, y, col.r, col.g, col.b, col.a); }
    static void drawPixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    static void drawRectFilled(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color col);
    static void drawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t borderSize, Color col);

    static void drawBitmap(uint32_t x, uint32_t y, uint32_t target_width, uint32_t target_height, const uint8_t* data);
    static void drawPNG(uint32_t x, uint32_t y, const uint8_t* data);

    static void initFont();
    static void deinitFont();
    static void setFontSize(uint32_t size);
    static void setFontColor(Color col);
    static void print(uint32_t x, uint32_t y, const char* string, bool alignRight = false);
    static void print(uint32_t x, uint32_t y, const wchar_t* string, bool alignRight = false);
    static uint32_t getTextWidth(const char* string);
    static uint32_t getTextWidth(const wchar_t* string);

private:
    static bool isBackBuffer;

    static uint8_t* tvBuffer;
    static uint32_t tvSize;
    static uint8_t* drcBuffer;
    static uint32_t drcSize;
};
