#include "ui_port.h"

#include <cstring>

#include "board_display.h"
#include "esp_heap_caps.h"

namespace {

constexpr int kStripRows = 48;
constexpr int kBytesPerRow = kUiLogicalW / 2;
constexpr int kOutW = kUiLogicalW / 2;
constexpr int kOutRows = kStripRows / 2;

uint8_t* s_strip = nullptr;
uint16_t* s_out = nullptr;

inline uint8_t nibble(const uint8_t* row, int x)
{
    const uint8_t b = row[x >> 1];
    return (x & 1) ? (b >> 4) : (b & 0x0F);
}

}  // namespace

void ui_present(UiDrawFn draw, void* ctx)
{
    if (!s_strip) {
        s_strip = static_cast<uint8_t*>(heap_caps_malloc(kStripRows * kBytesPerRow, MALLOC_CAP_8BIT));
        s_out = static_cast<uint16_t*>(heap_caps_malloc(kOutW * kOutRows * sizeof(uint16_t), MALLOC_CAP_8BIT));
        if (!s_strip || !s_out) {
            return;
        }
    }

    board_display_fill_rect(0, 0, kUiOffsetX, BOARD_DISPLAY_HEIGHT, 0xFFFF);
    board_display_fill_rect(kUiOffsetX + kOutW, 0, BOARD_DISPLAY_WIDTH - kUiOffsetX - kOutW,
                            BOARD_DISPLAY_HEIGHT, 0xFFFF);

    for (int y0 = 0; y0 < kUiLogicalH; y0 += kStripRows) {
        std::memset(s_strip, 0xFF, kStripRows * kBytesPerRow);
        epd_strip_select(y0, kStripRows);
        draw(s_strip, ctx);

        for (int oy = 0; oy < kOutRows; ++oy) {
            const uint8_t* r0 = s_strip + (oy * 2) * kBytesPerRow;
            const uint8_t* r1 = r0 + kBytesPerRow;
            for (int ox = 0; ox < kOutW; ++ox) {
                const int sum = nibble(r0, ox * 2) + nibble(r0, ox * 2 + 1) +
                                nibble(r1, ox * 2) + nibble(r1, ox * 2 + 1);
                const uint8_t g = static_cast<uint8_t>(sum * 255 / 60);
                s_out[oy * kOutW + ox] = static_cast<uint16_t>(((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3));
            }
        }
        board_display_blit(kUiOffsetX, y0 / 2, kOutW, kOutRows, s_out);
    }
}

bool ui_touch_to_logical(int tft_x, int tft_y, int& lx, int& ly)
{
    lx = (tft_x - kUiOffsetX) * 2;
    ly = tft_y * 2;
    return lx >= 0 && lx < kUiLogicalW && ly >= 0 && ly < kUiLogicalH;
}
