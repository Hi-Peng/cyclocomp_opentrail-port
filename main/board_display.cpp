#include "board_display.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_rom_sys.h"

namespace {

constexpr gpio_num_t kLcdCs = GPIO_NUM_15;
constexpr gpio_num_t kLcdDc = GPIO_NUM_2;
constexpr gpio_num_t kLcdReset = GPIO_NUM_3;
constexpr gpio_num_t kBacklight = GPIO_NUM_27;
constexpr gpio_num_t kTouchCs = GPIO_NUM_33;
constexpr gpio_num_t kTouchIrq = GPIO_NUM_36;
constexpr gpio_num_t kSpiMiso = GPIO_NUM_12;
constexpr gpio_num_t kSpiMosi = GPIO_NUM_13;
constexpr gpio_num_t kSpiClock = GPIO_NUM_14;

constexpr int kLcdClockHz = 40000000;
constexpr int kTouchClockHz = 2000000;
constexpr size_t kBytesPerPixel = 3;
constexpr size_t kLineBytes = BOARD_DISPLAY_WIDTH * kBytesPerPixel;

spi_device_handle_t s_lcd = nullptr;
spi_device_handle_t s_touch = nullptr;
DMA_ATTR uint8_t s_line[kLineBytes];

esp_err_t transmit(spi_device_handle_t device, const uint8_t* data, size_t size)
{
    spi_transaction_t transaction{};
    transaction.length = size * 8;
    transaction.tx_buffer = data;
    return spi_device_transmit(device, &transaction);
}

esp_err_t lcd_command(uint8_t command)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(kLcdDc, 0), "board_display", "set LCD command mode");
    return transmit(s_lcd, &command, sizeof(command));
}

esp_err_t lcd_data(const uint8_t* data, size_t size)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(kLcdDc, 1), "board_display", "set LCD data mode");
    return transmit(s_lcd, data, size);
}

esp_err_t lcd_data(uint8_t data)
{
    return lcd_data(&data, sizeof(data));
}

esp_err_t set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t x_end = x + width - 1;
    const uint16_t y_end = y + height - 1;
    const std::array<uint8_t, 4> columns{
        static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x),
        static_cast<uint8_t>(x_end >> 8), static_cast<uint8_t>(x_end)};
    const std::array<uint8_t, 4> rows{
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y),
        static_cast<uint8_t>(y_end >> 8), static_cast<uint8_t>(y_end)};

    ESP_RETURN_ON_ERROR(lcd_command(0x2A), "board_display", "set LCD column window");
    ESP_RETURN_ON_ERROR(lcd_data(columns.data(), columns.size()), "board_display", "write LCD columns");
    ESP_RETURN_ON_ERROR(lcd_command(0x2B), "board_display", "set LCD row window");
    ESP_RETURN_ON_ERROR(lcd_data(rows.data(), rows.size()), "board_display", "write LCD rows");
    return lcd_command(0x2C);
}

esp_err_t fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    if (width == 0 || height == 0 || x >= BOARD_DISPLAY_WIDTH || y >= BOARD_DISPLAY_HEIGHT) {
        return ESP_OK;
    }
    width = std::min<uint16_t>(width, BOARD_DISPLAY_WIDTH - x);
    height = std::min<uint16_t>(height, BOARD_DISPLAY_HEIGHT - y);

    const uint8_t red5 = static_cast<uint8_t>((color >> 11) & 0x1F);
    const uint8_t green6 = static_cast<uint8_t>((color >> 5) & 0x3F);
    const uint8_t blue5 = static_cast<uint8_t>(color & 0x1F);
    const uint8_t red = static_cast<uint8_t>((red5 << 3) | (red5 >> 2));
    const uint8_t green = static_cast<uint8_t>((green6 << 2) | (green6 >> 4));
    const uint8_t blue = static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2));
    for (uint16_t column = 0; column < width; ++column) {
        const size_t offset = static_cast<size_t>(column) * kBytesPerPixel;
        s_line[offset] = red;
        s_line[offset + 1] = green;
        s_line[offset + 2] = blue;
    }

    ESP_RETURN_ON_ERROR(set_window(x, y, width, height), "board_display", "set LCD fill window");
    for (uint16_t row = 0; row < height; ++row) {
        ESP_RETURN_ON_ERROR(lcd_data(s_line, static_cast<size_t>(width) * kBytesPerPixel),
                            "board_display", "write LCD pixels");
    }
    return ESP_OK;
}

esp_err_t write_touch_command(uint8_t command, uint16_t& value)
{
    uint8_t tx[3] = {command, 0, 0};
    uint8_t rx[3] = {};
    spi_transaction_t transaction{};
    transaction.length = sizeof(tx) * 8;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    ESP_RETURN_ON_ERROR(spi_device_transmit(s_touch, &transaction),
                        "board_display", "read XPT2046 sample");
    value = static_cast<uint16_t>(((static_cast<uint16_t>(rx[1]) << 8) | rx[2]) >> 3) & 0x0FFF;
    return ESP_OK;
}

esp_err_t read_touch_axis(uint8_t command, uint16_t& value)
{
    uint32_t total = 0;
    for (int sample = 0; sample < 4; ++sample) {
        uint16_t next = 0;
        ESP_RETURN_ON_ERROR(write_touch_command(command, next), "board_display", "sample touch axis");
        total += next;
    }
    value = static_cast<uint16_t>(total / 4);
    return ESP_OK;
}

esp_err_t initialize_panel()
{
    ESP_RETURN_ON_ERROR(gpio_set_level(kLcdReset, 0), "board_display", "assert LCD reset");
    esp_rom_delay_us(20000);
    ESP_RETURN_ON_ERROR(gpio_set_level(kLcdReset, 1), "board_display", "release LCD reset");
    esp_rom_delay_us(120000);

    ESP_RETURN_ON_ERROR(lcd_command(0x01), "board_display", "software reset");
    esp_rom_delay_us(120000);

    constexpr uint8_t positive_gamma[] = {
        0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
        0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F};
    constexpr uint8_t negative_gamma[] = {
        0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
        0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F};

    ESP_RETURN_ON_ERROR(lcd_command(0xE0), "board_display", "configure positive gamma");
    ESP_RETURN_ON_ERROR(lcd_data(positive_gamma, sizeof(positive_gamma)), "board_display", "write positive gamma");
    ESP_RETURN_ON_ERROR(lcd_command(0xE1), "board_display", "configure negative gamma");
    ESP_RETURN_ON_ERROR(lcd_data(negative_gamma, sizeof(negative_gamma)), "board_display", "write negative gamma");

    constexpr uint8_t power_1[] = {0x17, 0x15};
    constexpr uint8_t power_2[] = {0x41};
    constexpr uint8_t vcom[] = {0x00, 0x12, 0x80};
    constexpr uint8_t display_function[] = {0x02, 0x02, 0x3B};
    constexpr uint8_t adjust_control[] = {0xA9, 0x51, 0x2C, 0x82};

    ESP_RETURN_ON_ERROR(lcd_command(0xC0), "board_display", "configure LCD power");
    ESP_RETURN_ON_ERROR(lcd_data(power_1, sizeof(power_1)), "board_display", "write LCD power");
    ESP_RETURN_ON_ERROR(lcd_command(0xC1), "board_display", "configure LCD power mode");
    ESP_RETURN_ON_ERROR(lcd_data(power_2, sizeof(power_2)), "board_display", "write LCD power mode");
    ESP_RETURN_ON_ERROR(lcd_command(0xC5), "board_display", "configure VCOM");
    ESP_RETURN_ON_ERROR(lcd_data(vcom, sizeof(vcom)), "board_display", "write VCOM");
    ESP_RETURN_ON_ERROR(lcd_command(0x36), "board_display", "configure LCD orientation");
    ESP_RETURN_ON_ERROR(lcd_data(0x48), "board_display", "write LCD orientation");
    ESP_RETURN_ON_ERROR(lcd_command(0x3A), "board_display", "configure pixel format");
    ESP_RETURN_ON_ERROR(lcd_data(0x66), "board_display", "select RGB666 pixel format");
    ESP_RETURN_ON_ERROR(lcd_command(0xB0), "board_display", "configure LCD interface");
    ESP_RETURN_ON_ERROR(lcd_data(0x00), "board_display", "write LCD interface");
    ESP_RETURN_ON_ERROR(lcd_command(0xB1), "board_display", "configure frame rate");
    ESP_RETURN_ON_ERROR(lcd_data(0xA0), "board_display", "write frame rate");
    ESP_RETURN_ON_ERROR(lcd_command(0xB4), "board_display", "configure display inversion");
    ESP_RETURN_ON_ERROR(lcd_data(0x02), "board_display", "write display inversion");
    ESP_RETURN_ON_ERROR(lcd_command(0xB6), "board_display", "configure display function");
    ESP_RETURN_ON_ERROR(lcd_data(display_function, sizeof(display_function)), "board_display", "write display function");
    ESP_RETURN_ON_ERROR(lcd_command(0xB7), "board_display", "configure entry mode");
    ESP_RETURN_ON_ERROR(lcd_data(0xC6), "board_display", "write entry mode");
    ESP_RETURN_ON_ERROR(lcd_command(0xF7), "board_display", "configure display adjustment");
    ESP_RETURN_ON_ERROR(lcd_data(adjust_control, sizeof(adjust_control)), "board_display", "write display adjustment");

    ESP_RETURN_ON_ERROR(lcd_command(0x11), "board_display", "exit sleep mode");
    esp_rom_delay_us(120000);
    ESP_RETURN_ON_ERROR(lcd_command(0x29), "board_display", "turn display on");
    esp_rom_delay_us(25000);
    return ESP_OK;
}

}  // namespace

esp_err_t board_display_init()
{
    const gpio_config_t output_config{
        .pin_bit_mask = (1ULL << kLcdDc) | (1ULL << kLcdReset) | (1ULL << kBacklight),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), "board_display", "configure LCD GPIOs");
    ESP_RETURN_ON_ERROR(gpio_set_level(kLcdDc, 1), "board_display", "initialize LCD data mode");
    ESP_RETURN_ON_ERROR(gpio_set_level(kBacklight, 0), "board_display", "keep backlight off during setup");

    const gpio_config_t touch_irq_config{
        .pin_bit_mask = 1ULL << kTouchIrq,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&touch_irq_config), "board_display", "configure touch interrupt");

    const spi_bus_config_t bus_config{
        .mosi_io_num = kSpiMosi,
        .miso_io_num = kSpiMiso,
        .sclk_io_num = kSpiClock,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .data_io_default_level = false,
        .max_transfer_sz = static_cast<int>(kLineBytes),
        .dma_burst_size = 0,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        "board_display", "initialize shared LCD/touch SPI bus");

    const spi_device_interface_config_t lcd_device_config{
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = kLcdClockHz,
        .input_delay_ns = 0,
        .sample_point = SPI_SAMPLING_POINT_PHASE_0,
        .spics_io_num = kLcdCs,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = nullptr,
        .post_cb = nullptr};
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &lcd_device_config, &s_lcd),
                        "board_display", "add ILI9488 SPI device");

    const spi_device_interface_config_t touch_device_config{
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = kTouchClockHz,
        .input_delay_ns = 0,
        .sample_point = SPI_SAMPLING_POINT_PHASE_0,
        .spics_io_num = kTouchCs,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = nullptr,
        .post_cb = nullptr};
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &touch_device_config, &s_touch),
                        "board_display", "add XPT2046 SPI device");

    ESP_RETURN_ON_ERROR(initialize_panel(), "board_display", "initialize ILI9488 panel");
    return board_display_set_backlight(3);
}

void board_display_draw_test_ui()
{
    constexpr std::array<uint16_t, 6> colors{
        0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0};
    constexpr uint16_t band_height = BOARD_DISPLAY_HEIGHT / colors.size();
    for (size_t band = 0; band < colors.size(); ++band) {
        const uint16_t top = static_cast<uint16_t>(band * band_height);
        const uint16_t height = static_cast<uint16_t>(
            band + 1 == colors.size() ? BOARD_DISPLAY_HEIGHT - top : band_height);
        ESP_ERROR_CHECK(fill_rect(0, top, BOARD_DISPLAY_WIDTH, height, colors[band]));
    }
    ESP_ERROR_CHECK(fill_rect(0, 0, BOARD_DISPLAY_WIDTH, 4, 0x07FF));
}

bool board_display_read_touch(uint16_t& raw_x, uint16_t& raw_y)
{
    if (gpio_get_level(kTouchIrq) != 0) {
        return false;
    }
    ESP_ERROR_CHECK(read_touch_axis(0xD0, raw_x));
    ESP_ERROR_CHECK(read_touch_axis(0x90, raw_y));
    return true;
}

void board_display_draw_touch_marker(uint16_t x, uint16_t y)
{
    constexpr uint16_t marker_size = 9;
    const uint16_t left = x > marker_size / 2 ? x - marker_size / 2 : 0;
    const uint16_t top = y > marker_size / 2 ? y - marker_size / 2 : 0;
    ESP_ERROR_CHECK(fill_rect(left, top, marker_size, marker_size, 0xFFFF));
    ESP_ERROR_CHECK(fill_rect(x > 1 ? x - 1 : 0, y > 1 ? y - 1 : 0, 3, 3, 0x0000));
}

esp_err_t board_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    return fill_rect(x, y, w, h, color);
}

esp_err_t board_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* pixels)
{
    if (w == 0 || h == 0 || x >= BOARD_DISPLAY_WIDTH || y >= BOARD_DISPLAY_HEIGHT) {
        return ESP_OK;
    }
    const uint16_t cw = std::min<uint16_t>(w, BOARD_DISPLAY_WIDTH - x);
    const uint16_t ch = std::min<uint16_t>(h, BOARD_DISPLAY_HEIGHT - y);
    ESP_RETURN_ON_ERROR(set_window(x, y, cw, ch), "board_display", "set blit window");
    for (uint16_t row = 0; row < ch; ++row) {
        const uint16_t* src = pixels + static_cast<size_t>(row) * w;
        for (uint16_t col = 0; col < cw; ++col) {
            const uint16_t c = src[col];
            const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
            uint8_t* o = &s_line[static_cast<size_t>(col) * kBytesPerPixel];
            o[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            o[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            o[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        }
        ESP_RETURN_ON_ERROR(lcd_data(s_line, static_cast<size_t>(cw) * kBytesPerPixel),
                            "board_display", "write blit row");
    }
    return ESP_OK;
}

esp_err_t board_display_set_backlight(int level)
{
    static bool configured = false;
    // 0 = off, then roughly perceptual steps up to full.
    static constexpr uint32_t kDuty[4] = {0, 40, 130, 255};
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    if (!configured) {
        const ledc_timer_config_t timer{
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false};
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "board_display", "backlight timer");
        const ledc_channel_config_t channel{
            .gpio_num = kBacklight,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = kDuty[level],
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
            .flags = {.output_invert = 0},
            .deconfigure = false};
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), "board_display", "backlight channel");
        configured = true;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, kDuty[level]), "board_display", "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}