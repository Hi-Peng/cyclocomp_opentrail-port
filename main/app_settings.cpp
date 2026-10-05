#include "app_settings.h"

#include <algorithm>

#include "nvs.h"
#include "nvs_flash.h"

namespace app_settings {
namespace {

nvs_handle_t s_nvs = 0;

struct Values {
    int32_t ftp = 250;
    int32_t tz = 420;
    int32_t backlight = 3;
    int32_t miles = 0;
    int32_t clock24 = 1;
    int32_t usb = 0;
    int32_t offline = 0;
    int32_t mesh = 0;
} s_v;

void load(const char* key, int32_t& value)
{
    int32_t v;
    if (s_nvs && nvs_get_i32(s_nvs, key, &v) == ESP_OK) value = v;
}

void store(const char* key, int32_t value)
{
    if (!s_nvs) return;
    nvs_set_i32(s_nvs, key, value);
    nvs_commit(s_nvs);
}

}  // namespace

void begin()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK || nvs_open("opentrail", NVS_READWRITE, &s_nvs) != ESP_OK) {
        s_nvs = 0;
        return;
    }
    load("ftp", s_v.ftp);
    load("tz", s_v.tz);
    load("backlight", s_v.backlight);
    load("miles", s_v.miles);
    load("clock24", s_v.clock24);
    load("usb", s_v.usb);
    load("offline", s_v.offline);
    load("mesh", s_v.mesh);
}

int ftpWatts() { return s_v.ftp; }
void setFtpWatts(int w) { s_v.ftp = std::clamp(w, 50, 600); store("ftp", s_v.ftp); }
int tzMinutes() { return s_v.tz; }
void setTzMinutes(int m) { s_v.tz = std::clamp(m, -720, 840); store("tz", s_v.tz); }
int backlight() { return s_v.backlight; }
void setBacklight(int b) { s_v.backlight = std::clamp(b, 0, 3); store("backlight", s_v.backlight); }
bool useMiles() { return s_v.miles; }
void setUseMiles(bool on) { s_v.miles = on; store("miles", on); }
bool clock24h() { return s_v.clock24; }
void setClock24h(bool on) { s_v.clock24 = on; store("clock24", on); }
bool usbDrive() { return s_v.usb; }
void setUsbDrive(bool on) { s_v.usb = on; store("usb", on); }
bool showOffline() { return s_v.offline; }
void setShowOffline(bool on) { s_v.offline = on; store("offline", on); }
bool meshEnabled() { return s_v.mesh; }
void setMeshEnabled(bool on) { s_v.mesh = on; store("mesh", on); }

size_t dashText(char* out, size_t cap)
{
    if (!s_nvs || cap == 0) return 0;
    size_t len = cap;
    if (nvs_get_str(s_nvs, "dash", out, &len) != ESP_OK || len == 0) return 0;
    return len - 1;
}

void setDashText(const char* text)
{
    if (!s_nvs) return;
    nvs_set_str(s_nvs, "dash", text);
    nvs_commit(s_nvs);
}

}  // namespace app_settings
