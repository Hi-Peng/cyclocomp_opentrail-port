#include <algorithm>
#include <cstdint>

#include "board_display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_app.h"
#include "ui_port.h"

namespace {

uint16_t map_touch_axis(uint16_t raw, uint16_t extent)
{
    const uint32_t clamped = std::min<uint32_t>(raw, 4095);
    return static_cast<uint16_t>((4095 - clamped) * (extent - 1) / 4095);
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(board_display_init());
    ui_app_init();

    bool was_touched = false;
    while (true) {
        uint16_t raw_x = 0;
        uint16_t raw_y = 0;
        const bool touched = board_display_read_touch(raw_x, raw_y);
        if (touched && !was_touched) {
            int lx = 0, ly = 0;
            if (ui_touch_to_logical(map_touch_axis(raw_x, BOARD_DISPLAY_WIDTH),
                                    map_touch_axis(raw_y, BOARD_DISPLAY_HEIGHT), lx, ly)) {
                ui_app_tap(lx, ly);
            }
        }
        was_touched = touched;
        ui_app_tick(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
