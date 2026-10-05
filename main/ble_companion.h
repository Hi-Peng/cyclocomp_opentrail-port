#pragma once

#include <cstddef>
#include <cstdint>

// BLE GATT server for the OpenTrailPaper iOS companion app. Same service and
// characteristic UUIDs and payloads as the original firmware's ble_server.cpp.
// Implemented: Settings (0001), Status (0002), Dash layout (0009).
namespace ble_companion {

struct Status {
    uint8_t flags = 0;     // bit0 GPS fix, bit1 recording, bit2 route active
    uint8_t battery = 0;
    uint8_t sats = 0;
    uint8_t hr = 0;
    uint16_t power = 0;
    float speedKmh = 0;
    float remainKm = 0;
};

void begin();
bool phoneConnected();
// 6-digit code to show while the phone's pairing dialog is open; 0 otherwise.
uint32_t pairingCode();

void setStatus(const Status& s);   // notifies subscribers
void settingsChangedOnDevice();    // pushes the Settings characteristic

// Set by the phone writing Settings; cleared on read.
bool takeSettingsChanged();
// Current layout text served on reads of the Dash characteristic.
void setDashText(const char* text, size_t len);
// True (and `out` filled) when the phone wrote a complete new layout.
bool takeDashText(char* out, size_t cap);

}  // namespace ble_companion
