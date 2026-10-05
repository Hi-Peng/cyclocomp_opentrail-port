#include "ui_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_settings.h"
#include "ble_companion.h"
#include "board_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "opentrail/dash_layout.h"
#include "opentrail/ride_state.h"
#include "opentrail/ui_render.h"
#include "ui_port.h"

namespace {

constexpr char kTag[] = "ui_app";
// The status bar is only ~32 px tall on this TFT; accept a bigger target for finger taps.
constexpr int kHeaderTapH = 150;

enum Screen { SCREEN_DASH, SCREEN_SUMMARY, SCREEN_MENU, SCREEN_SENSORS, SCREEN_ROUTES,
              SCREEN_HISTORY, SCREEN_SETTINGS };

Screen s_screen = SCREEN_DASH;
RideState s_ride;
RideSummary s_summary;
DashLayout s_layout;
uint32_t s_lastPairCode = 0;
uint32_t s_lastStatusMs = 0;

DashPages s_pages;

void publish_dash_text()
{
    static char text[2048];
    const size_t n = dashSerializePages(s_pages, text, sizeof(text));
    ble_companion::setDashText(text, n);
}

void select_layout()
{
    for (int i = 0; i < s_pages.count; ++i) {
        if (s_pages.pages[i].kind == DP_FIELDS && s_pages.pages[i].layout.count > 0) {
            s_layout = s_pages.pages[i].layout;
            return;
        }
    }
    s_layout = dashDefaultLayout();
}

bool apply_dash_text(const char* text)
{
    DashPages pages;
    if (!dashParsePages(text, pages)) return false;
    s_pages = pages;
    select_layout();
    publish_dash_text();
    return true;
}bool s_dirty = true;
uint32_t s_last_tick_ms = 0;
float s_phase = 0;
double s_sumSpeed = 0;
uint32_t s_sumSamples = 0;

bool in_rect(const EpdRect& r, int x, int y)
{
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

void sync_ride_config()
{
    s_ride.ftpW = static_cast<uint16_t>(app_settings::ftpWatts());
    s_ride.tzMin = static_cast<int16_t>(app_settings::tzMinutes());
    s_ride.useMiles = app_settings::useMiles();
    s_ride.clock24h = app_settings::clock24h();
    s_ride.showOffline = app_settings::showOffline();
}

void reset_ride_data()
{
    s_ride.recording = false;
    s_ride.elapsedS = s_ride.movingS = 0;
    s_ride.distanceM = 0;
    s_sumSpeed = 0;
    s_sumSamples = 0;
}

void build_summary()
{
    s_summary = RideSummary{};
    s_summary.distanceM = s_ride.distanceM;
    s_summary.movingS = s_ride.movingS;
    s_summary.elapsedS = s_ride.elapsedS;
    s_summary.avgSpeedKmh = s_sumSamples ? static_cast<float>(s_sumSpeed / s_sumSamples) : 0.0f;
    s_summary.avgPowerW = 180;
    s_summary.normPowerW = 195;
    s_summary.avgHrBpm = 142;
    s_summary.tzMin = s_ride.tzMin;
    s_summary.useMiles = s_ride.useMiles;
}

void fill_list(ListRow* rows, int& count, const char*& footer)
{
    count = 0;
    switch (s_screen) {
        case SCREEN_SENSORS:
            footer = "sensors are not available in this port yet";
            std::snprintf(rows[0].title, sizeof(rows[0].title), "No sensors found");
            std::snprintf(rows[0].subtitle, sizeof(rows[0].subtitle), "BLE scanning is not ported");
            count = 1;
            break;
        case SCREEN_ROUTES:
            footer = "put .gpx files in /routes on the SD card";
            std::snprintf(rows[0].title, sizeof(rows[0].title), "No routes");
            std::snprintf(rows[0].subtitle, sizeof(rows[0].subtitle), "SD card is not ported yet");
            count = 1;
            break;
        default:
            footer = "rides upload from /rides on the SD card";
            if (s_ride.recording) {
                std::snprintf(rows[0].title, sizeof(rows[0].title), "Recording in progress");
                std::snprintf(rows[0].subtitle, sizeof(rows[0].subtitle), "%.1f km - %lu:%02lu:%02lu",
                              s_ride.distanceM / 1000.0,
                              static_cast<unsigned long>(s_ride.elapsedS / 3600),
                              static_cast<unsigned long>(s_ride.elapsedS / 60 % 60),
                              static_cast<unsigned long>(s_ride.elapsedS % 60));
                rows[0].inverted = true;
                count = 1;
            }
            break;
    }
}

void render_screen(uint8_t* fb)
{
    const RideState& s = s_ride;
    switch (s_screen) {
        case SCREEN_DASH:
            ui_render_dashboard(s, false, s_layout, fb);
            break;
        case SCREEN_SUMMARY:
            ui_render_dashboard(s, false, s_layout, fb);
            ui_render_summary(s_summary, fb, false);
            break;
        case SCREEN_MENU: {
            MenuInfo m;
            m.recording = s.recording;
            m.gpsReady = false;
            m.sdOk = false;
            m.batteryPercent = s.batteryPercent;
            m.rideDistanceM = s.distanceM;
            m.rideElapsedS = s.elapsedS;
            m.useMiles = s.useMiles;
            ui_render_menu(m, fb);
            ui::statusBar(s, fb, "MENU");
            break;
        }
        case SCREEN_SENSORS:
        case SCREEN_ROUTES:
        case SCREEN_HISTORY: {
            ListRow rows[kMenuRowCount] = {};
            int count = 0;
            const char* footer = "";
            const char* title = s_screen == SCREEN_SENSORS ? "SENSORS"
                                : s_screen == SCREEN_ROUTES ? "NAVIGATE" : "RIDES";
            fill_list(rows, count, footer);
            ui_render_list(title, rows, count, footer, fb);
            ui::statusBar(s, fb, title);
            break;
        }
        case SCREEN_SETTINGS: {
            SettingsInfo si{app_settings::showOffline(), app_settings::ftpWatts(), app_settings::tzMinutes(),
                            app_settings::backlight(), app_settings::useMiles(), app_settings::usbDrive(),
                            app_settings::meshEnabled()};
            ui_render_settings(si, fb);
            ui::statusBar(s, fb, "SETTINGS");
            break;
        }
    }
}

void render(uint8_t* fb, void*)
{
    render_screen(fb);
    if (const uint32_t code = ble_companion::pairingCode()) ui_render_pairing(code, fb);
}

void present()
{
    const int64_t t0 = esp_timer_get_time();
    ui_present(render, nullptr);
    ESP_LOGI(kTag, "screen %d drawn in %lld ms, free heap %u", static_cast<int>(s_screen),
             (esp_timer_get_time() - t0) / 1000, static_cast<unsigned>(esp_get_free_heap_size()));
    s_dirty = false;
}

void handle_settings_tap(int x, int y)
{
    if (y < kHeaderTapH) {
        s_screen = SCREEN_MENU;
        s_dirty = true;
        return;
    }
    const int row = (y - kMenuRowTop) / kSettingsRowH;
    if (row < 0 || row >= kSettingsRowCount) return;

    const bool toggleRow = row == kSettingsUnitsRow || row == kSettingsUsbRow ||
                           row == kSettingsOfflineRow || row == kSettingsMeshRow;
    if (!toggleRow) {
        const bool minus = x >= kSettingsMinusX && x < kSettingsMinusX + kSettingsBtn;
        const bool plus = x >= kSettingsPlusX && x < kSettingsPlusX + kSettingsBtn;
        if (!minus && !plus) return;
        const int dir = plus ? 1 : -1;
        if (row == 0) app_settings::setFtpWatts(app_settings::ftpWatts() + dir * 5);
        if (row == 1) app_settings::setTzMinutes(app_settings::tzMinutes() + dir * 30);
        if (row == kSettingsBacklightRow) {
            const int bl = std::min(3, std::max(0, app_settings::backlight() + dir));
            app_settings::setBacklight(bl);
            board_display_set_backlight(bl);
        }
    } else {
        if (x < settingsToggleX(row) || x >= settingsToggleX(row) + settingsToggleW(row)) return;
        if (row == kSettingsUnitsRow) app_settings::setUseMiles(!app_settings::useMiles());
        else if (row == kSettingsUsbRow) app_settings::setUsbDrive(!app_settings::usbDrive());
        else if (row == kSettingsMeshRow) app_settings::setMeshEnabled(!app_settings::meshEnabled());
        else app_settings::setShowOffline(!app_settings::showOffline());
    }
    sync_ride_config();
    ble_companion::settingsChangedOnDevice();
    s_dirty = true;
}

}  // namespace

void ui_app_init()
{
    app_settings::begin();
    board_display_set_backlight(app_settings::backlight());
    s_pages = dashDefaultPages();
    static char saved[2048];
    if (app_settings::dashText(saved, sizeof(saved)) > 0) apply_dash_text(saved);
    select_layout();
    publish_dash_text();
    ble_companion::begin();
    s_ride.batteryPercent = 78;
    reset_ride_data();
    sync_ride_config();
    s_dirty = true;
}

void ui_app_tap(int x, int y)
{
    ESP_LOGI(kTag, "tap logical (%d,%d) on screen %d", x, y, static_cast<int>(s_screen));
    switch (s_screen) {
        case SCREEN_DASH:
            if (y < kHeaderTapH) {
                s_screen = SCREEN_MENU;
                s_dirty = true;
            }
            break;
        case SCREEN_SUMMARY:
            if (in_rect(kResumeButton, x, y)) {
                s_screen = SCREEN_DASH;
            } else if (in_rect(kSaveButton, x, y) || in_rect(kDiscardButton, x, y)) {
                reset_ride_data();
                s_screen = SCREEN_DASH;
            } else {
                return;
            }
            s_dirty = true;
            break;
        case SCREEN_MENU: {
            const int row = (y - kMenuRowTop) / kMenuRowH;
            if (y >= kMenuRowTop && row >= 0 && row < kMenuRowCount) {
                switch (row) {
                    case 0:
                        if (s_ride.recording) {
                            build_summary();
                            s_screen = SCREEN_SUMMARY;
                        } else {
                            s_ride.recording = true;
                            s_screen = SCREEN_DASH;
                        }
                        break;
                    case 1: s_screen = SCREEN_ROUTES; break;
                    case 2: s_screen = SCREEN_SENSORS; break;
                    case 3: s_screen = SCREEN_HISTORY; break;
                    default: s_screen = SCREEN_SETTINGS; break;
                }
            } else {
                s_screen = SCREEN_DASH;
            }
            s_dirty = true;
            break;
        }
        case SCREEN_SENSORS:
        case SCREEN_ROUTES:
        case SCREEN_HISTORY:
            s_screen = SCREEN_MENU;
            s_dirty = true;
            break;
        case SCREEN_SETTINGS:
            handle_settings_tap(x, y);
            break;
    }
}

void ui_app_tick(uint32_t now_ms)
{
    if (ble_companion::takeSettingsChanged()) {
        board_display_set_backlight(app_settings::backlight());
        sync_ride_config();
        s_dirty = true;
    }
    static char dashIn[2048];
    if (ble_companion::takeDashText(dashIn, sizeof(dashIn)) && apply_dash_text(dashIn)) {
        app_settings::setDashText(dashIn);
        s_dirty = true;
    }
    const uint32_t code = ble_companion::pairingCode();
    if (code != s_lastPairCode) {
        s_lastPairCode = code;
        s_dirty = true;
    }
    if (now_ms - s_lastStatusMs >= 1000) {
        s_lastStatusMs = now_ms;
        ble_companion::Status st;
        st.flags = (s_ride.recording ? 2 : 0);
        st.battery = s_ride.batteryPercent;
        st.sats = s_ride.satellites;
        st.hr = s_ride.heartRateBpm == 0xFF ? 0 : s_ride.heartRateBpm;
        st.power = s_ride.powerW == 0xFFFF ? 0 : s_ride.powerW;
        st.speedKmh = s_ride.speedKmh;
        ble_companion::setStatus(st);
    }
    // Simulated sensors run all the time so the full dashboard is visible.
    if (now_ms - s_last_tick_ms >= 1000) {
        s_last_tick_ms = now_ms;
        s_phase += 0.3f;
        s_ride.speedKmh = 25.0f + 8.0f * sinf(s_phase);
        s_ride.powerW = static_cast<uint16_t>(180 + 60 * sinf(s_phase * 1.3f));
        s_ride.power3sW = s_ride.powerW;
        s_ride.heartRateBpm = static_cast<uint8_t>(140 + 10 * sinf(s_phase * 0.5f));
        s_ride.cadenceRpm = static_cast<uint8_t>(85 + 6 * sinf(s_phase * 0.8f));
        s_ride.gradePercent = 3.0f * sinf(s_phase * 0.4f);
        s_ride.gradeValid = true;
        s_ride.hrConnected = s_ride.powerConnected = s_ride.cadenceConnected = true;
        if (s_ride.recording) {
            s_ride.elapsedS++;
            s_ride.movingS++;
            s_ride.distanceM += s_ride.speedKmh / 3.6;
            s_sumSpeed += s_ride.speedKmh;
            ++s_sumSamples;
        }
        if (s_screen == SCREEN_DASH) s_dirty = true;
    }
    if (s_dirty) present();
}
