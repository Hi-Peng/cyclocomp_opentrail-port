# Cyclocomp OpenTrail Port

An ESP-IDF firmware port of the OpenTrailPaper cycling-computer interface for
an ESP32 board with a 320×480 SPI TFT and resistive touch panel. The project
adapts OpenTrail's dashboard renderer to the smaller TFT and includes a BLE
GATT service for the OpenTrailPaper phone companion app.

> **Port status:** This is a work in progress. The display UI and BLE settings /
> dashboard exchange are implemented, but live GPS, sensor discovery, route
> navigation, ride-file storage, and SD-card support are not yet ported.
> Dashboard ride metrics currently come from a built-in simulator.

## Features

- Touch-driven dashboard, menu, ride summary, and settings screens.
- OpenTrail dashboard layout text parsing and serialization, with rider
  settings and the saved layout persisted through ESP-IDF NVS.
- BLE peripheral service for the companion app:
  - Settings read/write and device-to-phone updates.
  - Ride status reads and notifications.
  - Dashboard layout reads and writes, including chunked transfers.
- SPI TFT output using a strip-based renderer to avoid requiring a full-size
  framebuffer in PSRAM.
- Simulated speed, power, heart rate, cadence, and grade values so the
  dashboard can be exercised before sensors are ported.

The Sensors screen reports that BLE sensor scanning is not available yet.
Routes and ride history are placeholders: the UI mentions GPX files in
`/routes` and rides in `/rides`, but SD-card access and those file workflows
are not implemented.

## Hardware

The board display driver is configured for a 320×480 TFT and an XPT2046-style
touch controller on the same SPI bus:

| Signal | ESP32 GPIO |
| --- | ---: |
| LCD chip select | 15 |
| LCD data/command | 2 |
| LCD reset | 3 |
| Backlight | 27 |
| Touch chip select | 33 |
| Touch interrupt | 36 |
| SPI MISO | 12 |
| SPI MOSI | 13 |
| SPI clock | 14 |

The LCD SPI clock is configured for 40 MHz and the touch clock for 2 MHz. Check
these pins and the panel initialization sequence against your board before
connecting or flashing hardware.

## Build and flash

Install ESP-IDF and make sure its environment is active so `idf.py` is
available. From the repository root:

```console
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the board's serial port. If your board uses a different
ESP32-family chip, select its matching ESP-IDF target instead of `esp32`.
`sdkconfig.defaults` enables NimBLE in BLE-only mode; ESP-IDF creates the
build-specific `sdkconfig` during configuration.

To build without flashing, run only `idf.py build`. A full clean can be done
with `idf.py fullclean` followed by `idf.py build`.

## Project layout

- `main/main.cpp` — application entry point and touch/tick loop.
- `main/board_display.*` — TFT, touch-controller, SPI, and backlight driver.
- `main/ui_port.*` — adapts the OpenTrail renderer to the TFT framebuffer and
  touch coordinates.
- `main/ui_app.*` — screen navigation, dashboard state, simulated ride data,
  and application settings integration.
- `main/app_settings.*` — NVS-backed rider preferences and dashboard layout.
- `main/ble_companion.*` — BLE GATT peripheral and companion-app data exchange.
- `main/opentrail/` — ported dashboard, rendering, font, radar, and workout
  modules.
- `sdkconfig.defaults` — default BLE/NimBLE configuration.

## Current limitations

- Ride data is simulated; GPS, BLE sensor scanning, and live sensor input are
  not connected.
- Route loading, map data, SD-card access, USB-drive mode, and ride-file
  persistence are not implemented by this port.
- Summary values for average power, normalized power, and heart rate are
  placeholders.
- The display pins and panel initialization are board-specific.
