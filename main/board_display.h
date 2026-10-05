#pragma once

#include <cstdint>

#include "esp_err.h"

constexpr uint16_t BOARD_DISPLAY_WIDTH = 320;
constexpr uint16_t BOARD_DISPLAY_HEIGHT = 480;

esp_err_t board_display_init();
void board_display_draw_test_ui();
bool board_display_read_touch(uint16_t& raw_x, uint16_t& raw_y);
void board_display_draw_touch_marker(uint16_t x, uint16_t y);
esp_err_t board_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t board_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* pixels);
esp_err_t board_display_set_backlight(int level);  // 0 off .. 3 bright
