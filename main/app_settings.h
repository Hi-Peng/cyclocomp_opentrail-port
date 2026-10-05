#pragma once

#include <cstddef>
#include <cstdint>

// NVS-backed rider settings (the subset of OpenTrailPaper's settings the UI edits).
namespace app_settings {

void begin();

int ftpWatts();
void setFtpWatts(int w);
int tzMinutes();
void setTzMinutes(int m);
int backlight();   // 0 off .. 3 bright
void setBacklight(int b);
bool useMiles();
void setUseMiles(bool on);
bool clock24h();
void setClock24h(bool on);
bool usbDrive();
void setUsbDrive(bool on);
bool showOffline();
void setShowOffline(bool on);
bool meshEnabled();
void setMeshEnabled(bool on);

// Dashboard layout text (same format as OpenTrailPaper's dashboard.cfg).
// dashText() returns 0 when nothing is stored.
size_t dashText(char* out, size_t cap);
void setDashText(const char* text);

}  // namespace app_settings
