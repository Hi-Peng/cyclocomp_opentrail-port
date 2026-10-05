#pragma once

#include <cstdint>

void ui_app_init();
// A tap in OpenTrailPaper's 540x960 logical coordinates.
void ui_app_tap(int x, int y);
// Call often; advances the simulated ride and repaints when something changed.
void ui_app_tick(uint32_t now_ms);
