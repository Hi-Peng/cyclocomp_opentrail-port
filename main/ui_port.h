#pragma once

#include <cstdint>

#include "opentrail/epd_compat.h"

// OpenTrailPaper's renderer draws a 540x960 4-bit screen. That framebuffer
// (259 KB) does not fit without PSRAM, so a screen is drawn as a series of
// horizontal strips, each halved 2x2 and pushed to the 320x480 TFT.
// The 270x480 result is centred; logical UI coordinates are 2x the pixel
// position inside that area.
constexpr int kUiLogicalW = 540;
constexpr int kUiLogicalH = 960;
constexpr int kUiOffsetX = 25;

using UiDrawFn = void (*)(uint8_t* fb, void* ctx);

void ui_present(UiDrawFn draw, void* ctx);
bool ui_touch_to_logical(int tft_x, int tft_y, int& lx, int& ly);
