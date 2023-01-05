#include "DrawUtils.h"

#include "logger.h"
#include "utils.h"
#include <coreinit/cache.h>
#include <coreinit/memory.h>
#include <coreinit/screen.h>
#include <cstdlib>
#include <png.h>


// buffer width
#define TV_WIDTH  0x500
#define DRC_WIDTH 0x380

bool DrawUtils::isBackBuffer;

uint8_t *DrawUtils::tvBuffer  = nullptr;
uint32_t DrawUtils::tvSize    = 0;
uint8_t *DrawUtils::drcBuffer = nullptr;
uint32_t DrawUtils::drcSize   = 0;
static SFT pFont              = {};

static Color font_col(0xFFFFFFFF);

void DrawUtils::initBuffers(void *tvBuffer_, uint32_t tvSize_, void *drcBuffer_, uint32_t drcSize_) {
    DrawUtils::tvBuffer  = (uint8_t *) tvBuffer_;
    DrawUtils::tvSize    = tvSize_;
    DrawUtils::drcBuffer = (uint8_t *) drcBuffer_;
    DrawUtils::drcSize   = drcSize_;
}

void DrawUtils::beginDraw() {
    uint32_t pixel = *(uint32_t *) tvBuffer;

    // check which buffer is currently used
    OSScreenPutPixelEx(SCREEN_TV, 0, 0, 0xABCDEF90);
    if (*(uint32_t *) tvBuffer == 0xABCDEF90) {
        isBackBuffer = false;
    } else {
        isBackBuffer = true;
    }

    // restore the pixel we used for checking
    *(uint32_t *) tvBuffer = pixel;
}

void DrawUtils::endDraw() {
    // OSScreenFlipBuffersEx already flushes the cache?
    // DCFlushRange(tvBuffer, tvSize);
    // DCFlushRange(drcBuffer, drcSize);

    OSScreenFlipBuffersEx(SCREEN_DRC);
    OSScreenFlipBuffersEx(SCREEN_TV);
}

void DrawUtils::clear(Color col) {
    OSScreenClearBufferEx(SCREEN_TV, col.color);
    OSScreenClearBufferEx(SCREEN_DRC, col.color);
}

void DrawUtils::drawPixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float opacity = a / 255.0f;

    // put pixel in the drc buffer
    uint32_t i = (x + y * DRC_WIDTH) * 4;
    if (i + 3 < drcSize / 2) {
        if (isBackBuffer) {
            i += drcSize / 2;
        }
        if (a == 0xFF) {
            drcBuffer[i]     = r;
            drcBuffer[i + 1] = g;
            drcBuffer[i + 2] = b;
        } else {
            drcBuffer[i]     = r * opacity + drcBuffer[i] * (1 - opacity);
            drcBuffer[i + 1] = g * opacity + drcBuffer[i + 1] * (1 - opacity);
            drcBuffer[i + 2] = b * opacity + drcBuffer[i + 2] * (1 - opacity);
        }
    }

    uint32_t USED_TV_WIDTH = TV_WIDTH;
    float scale            = 1.5f;
    if (DrawUtils::tvSize == 0x00FD2000) {
        USED_TV_WIDTH = 1920;
        scale         = 2.25f;
    }

    // scale and put pixel in the tv buffer
    for (uint32_t yy = (y * scale); yy < ((y * scale) + (uint32_t) scale); yy++) {
        for (uint32_t xx = (x * scale); xx < ((x * scale) + (uint32_t) scale); xx++) {
            uint32_t i = (xx + yy * USED_TV_WIDTH) * 4;
            if (i + 3 < tvSize / 2) {
                if (isBackBuffer) {
                    i += tvSize / 2;
                }
                if (a == 0xFF) {
                    tvBuffer[i]     = r;
                    tvBuffer[i + 1] = g;
                    tvBuffer[i + 2] = b;
                } else {
                    tvBuffer[i]     = r * opacity + tvBuffer[i] * (1 - opacity);
                    tvBuffer[i + 1] = g * opacity + tvBuffer[i + 1] * (1 - opacity);
                    tvBuffer[i + 2] = b * opacity + tvBuffer[i + 2] * (1 - opacity);
                }
            }
        }
    }
}

void DrawUtils::drawRectFilled(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color col) {
    for (uint32_t yy = y; yy < y + h; yy++) {
        for (uint32_t xx = x; xx < x + w; xx++) {
            drawPixel(xx, yy, col);
        }
    }
}

void DrawUtils::drawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t borderSize, Color col) {
    drawRectFilled(x, y, w, borderSize, col);
    drawRectFilled(x, y + h - borderSize, w, borderSize, col);
    drawRectFilled(x, y, borderSize, h, col);
    drawRectFilled(x + w - borderSize, y, borderSize, h, col);
}

void DrawUtils::drawBitmap(uint32_t x, uint32_t y, uint32_t target_width, uint32_t target_height, const uint8_t *data) {
    if (data[0] != 'B' || data[1] != 'M') {
        // invalid header
        return;
    }

    uint32_t dataPos = __builtin_bswap32(*(uint32_t *) &(data[0x0A]));
    uint32_t width   = __builtin_bswap32(*(uint32_t *) &(data[0x12]));
    uint32_t height  = __builtin_bswap32(*(uint32_t *) &(data[0x16]));

    if (dataPos == 0) {
        dataPos = 54;
    }

    data += dataPos;

    // TODO flip image since bitmaps are stored upside down

    for (uint32_t yy = y; yy < y + target_height; yy++) {
        for (uint32_t xx = x; xx < x + target_width; xx++) {
            uint32_t i = (((xx - x) * width / target_width) + ((yy - y) * height / target_height) * width) * 3;
            drawPixel(xx, yy, data[i + 2], data[i + 1], data[i], 0xFF);
        }
    }
}

static void png_read_data(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
    void **data = (void **) png_get_io_ptr(png_ptr);

    memcpy(outBytes, *data, byteCountToRead);
    *((uint8_t **) data) += byteCountToRead;
}

void DrawUtils::drawPNG(uint32_t x, uint32_t y, const uint8_t *data) {
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png_ptr == nullptr) {
        return;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return;
    }

    png_set_read_fn(png_ptr, (void *) &data, png_read_data);

    png_read_info(png_ptr, info_ptr);

    uint32_t width  = 0;
    uint32_t height = 0;
    int bitDepth    = 0;
    int colorType   = -1;
    uint32_t retval = png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);
    if (retval != 1) {
        return;
    }

    uint32_t bytesPerRow = png_get_rowbytes(png_ptr, info_ptr);
    auto *rowData        = new uint8_t[bytesPerRow];

    for (uint32_t yy = y; yy < y + height; yy++) {
        png_read_row(png_ptr, (png_bytep) rowData, nullptr);

        for (uint32_t xx = x; xx < x + width; xx++) {
            if (colorType == PNG_COLOR_TYPE_RGB_ALPHA) {
                uint32_t i = (xx - x) * 4;
                drawPixel(xx, yy, rowData[i], rowData[i + 1], rowData[i + 2], rowData[i + 3]);
            } else if (colorType == PNG_COLOR_TYPE_RGB) {
                uint32_t i = (xx - x) * 3;
                drawPixel(xx, yy, rowData[i], rowData[i + 1], rowData[i + 2], 0xFF);
            }
        }
    }

    delete[] rowData;
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
}

bool DrawUtils::initFont() {
    void *font    = nullptr;
    uint32_t size = 0;
    OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &font, &size);

    if (font && size) {
        pFont.xScale = 20;
        pFont.yScale = 20,
        pFont.flags  = SFT_DOWNWARD_Y;
        pFont.font   = sft_loadmem(font, size);
        if (!pFont.font) {
            return false;
        }
        OSMemoryBarrier();
        return true;
    }
    return false;
}

void DrawUtils::deinitFont() {
    sft_freefont(pFont.font);
    pFont.font = nullptr;
    pFont      = {};
}

void DrawUtils::setFontSize(uint32_t size) {
    pFont.xScale = size;
    pFont.yScale = size;
    SFT_LMetrics metrics;
    sft_lmetrics(&pFont, &metrics);
}

void DrawUtils::setFontColor(Color col) {
    font_col = col;
}

static void draw_freetype_bitmap(SFT_Image *bmp, int32_t x, int32_t y) {
    int32_t i, j, p, q;

    int32_t x_max = x + bmp->width;
    int32_t y_max = y + bmp->height;

    auto *src = (uint8_t *) bmp->pixels;

    for (i = x, p = 0; i < x_max; i++, p++) {
        for (j = y, q = 0; j < y_max; j++, q++) {
            if (i < 0 || j < 0 || i >= SCREEN_WIDTH || j >= SCREEN_HEIGHT) {
                continue;
            }

            float opacity = src[q * bmp->width + p] / 255.0f;
            DrawUtils::drawPixel(i, j, font_col.r, font_col.g, font_col.b, font_col.a * opacity);
        }
    }
}

void DrawUtils::print(uint32_t x, uint32_t y, const char *string, bool alignRight) {
    auto *buffer = new wchar_t[strlen(string) + 1];

    size_t num = mbstowcs(buffer, string, strlen(string));
    if (num > 0) {
        buffer[num] = 0;
    } else {
        wchar_t *tmp = buffer;
        while ((*tmp++ = *string++))
            ;
    }

    print(x, y, buffer, alignRight);
    delete[] buffer;
}

void DrawUtils::print(uint32_t x, uint32_t y, const wchar_t *string, bool alignRight) {
    auto penX = (int32_t) x;
    auto penY = (int32_t) y;

    if (alignRight) {
        penX -= getTextWidth(string);
    }

    uint16_t textureWidth = 0, textureHeight = 0;
    for (; *string; string++) {
        SFT_Glyph gid; //  unsigned long gid;
        if (sft_lookup(&pFont, *string, &gid) >= 0) {
            SFT_GMetrics mtx;
            if (sft_gmetrics(&pFont, gid, &mtx) < 0) {
                DEBUG_FUNCTION_LINE_ERR("Failed to get glyph metrics");
                return;
            }

            if (*string == '\n') {
                penY += mtx.minHeight;
                penX = x;
                continue;
            }

            textureWidth  = (mtx.minWidth + 3) & ~3;
            textureHeight = mtx.minHeight;

            SFT_Image img = {
                    .pixels = nullptr,
                    .width  = textureWidth,
                    .height = textureHeight,
            };

            if (textureWidth == 0) {
                textureWidth = 4;
            }
            if (textureHeight == 0) {
                textureHeight = 4;
            }

            auto buffer = make_unique_nothrow<uint8_t[]>((uint32_t) (img.width * img.height));
            if (!buffer) {
                DEBUG_FUNCTION_LINE_ERR("Failed to allocate memory for glyph");
                return;
            }
            img.pixels = buffer.get();
            if (sft_render(&pFont, gid, img) < 0) {
                DEBUG_FUNCTION_LINE_ERR("Failed to render glyph");
                return;
            } else {
                draw_freetype_bitmap(&img, (int32_t) (penX + mtx.leftSideBearing), (int32_t) (penY + mtx.yOffset));
                penX += (int32_t) mtx.advanceWidth;
            }
        }
    }
}

uint32_t DrawUtils::getTextWidth(const char *string) {
    auto *buffer = new wchar_t[strlen(string) + 1];

    size_t num = mbstowcs(buffer, string, strlen(string));
    if (num > 0) {
        buffer[num] = 0;
    } else {
        wchar_t *tmp = buffer;
        while ((*tmp++ = *string++))
            ;
    }

    uint32_t width = getTextWidth(buffer);
    delete[] buffer;

    return width;
}

uint32_t DrawUtils::getTextWidth(const wchar_t *string) {
    uint32_t width = 0;

    for (; *string; string++) {
        SFT_Glyph gid; //  unsigned long gid;
        if (sft_lookup(&pFont, *string, &gid) >= 0) {
            SFT_GMetrics mtx;
            if (sft_gmetrics(&pFont, gid, &mtx) < 0) {
                DEBUG_FUNCTION_LINE_ERR("bad glyph metrics");
            }
            width += (int32_t) mtx.advanceWidth;
        }
    }

    return (uint32_t) width;
}
