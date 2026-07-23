# ESP32-S3 AI Watch UI

Firmware for an ESP32-S3 N16R8 board with a 128x128 ST7735 display, buttons, Wi-Fi setup portal, AI voice call mode, weather/server dashboard, LED controls, and MPU6050 sensor pages.

## Features

- ST7735 128x128 watch-style home screen
- Wi-Fi captive portal with QR setup page
- Weather and server status display
- AI voice call mode with automatic listen/stop/reply loop
- MAX98357A speaker playback and I2S microphone recording
- LED control page with effects
- MPU6050 pages:
  - raw accel/gyro/temp
  - angle page
  - level page
  - odometer estimate
  - motion/event page
- UI flow blueprint in `docs/ui-flow.md` for future EEZ Studio/LVGL migration

## Hardware Pins

### Display

| Signal | ESP32-S3 GPIO |
| --- | --- |
| LED | GPIO15 |
| SCL | GPIO12 |
| SDA | GPIO11 |
| DC/RS | GPIO9 |
| CS | GPIO10 |
| RST | GPIO8 |

### Buttons

Buttons are active-low with internal pullups.

| Button | GPIO |
| --- | --- |
| P4 | GPIO4 |
| P5 | GPIO5 |
| P6 | GPIO6 |
| P7 | GPIO7 |

### Audio

| Signal | ESP32-S3 GPIO |
| --- | --- |
| I2S BCLK | GPIO16 |
| I2S WS/LRC | GPIO17 |
| Mic SD | GPIO18 |
| Speaker DIN | GPIO14 |

### MPU6050

| MPU6050 | ESP32-S3 GPIO |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| SCL | GPIO41 |
| SDA | GPIO42 |
| INT | optional GPIO13 |

## Build

This project uses PlatformIO.

```bash
pio run
```

Upload:

```bash
pio run --target upload --upload-port /dev/ttyACM0
```

## Credentials

Do not commit real Wi-Fi passwords, service accounts, tokens, private keys, server hostnames, or private API base URLs.

The firmware stores Wi-Fi and service credentials in ESP32 Preferences at runtime. Optional provisioning can be done with build flags such as `PROVISION_SSID`, `PROVISION_PASSWORD`, `PROVISION_SERVICE_USER`, `PROVISION_SERVICE_PASSWORD`, and `SERVICE_BASE_URL`, but keep those in a local ignored file or private build environment.

Example private build flag:

```ini
build_flags =
    -DSERVICE_BASE_URL=\"http://example.invalid/base-path\"
```

## UI Migration

The current firmware still mixes raw ST7735 drawing and LVGL. See `docs/ui-flow.md` for the planned page graph and EEZ Studio migration blueprint.
