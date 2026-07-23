# ESP32-S3 UI Flow Blueprint

This document is the migration blueprint for rebuilding the current hand-coded UI in a visual UI tool such as EEZ Studio/EEZ Flow.

## Goals

- Replace page-specific hard-coded button logic with a consistent page state machine.
- Keep the 128x128 screen readable by using stable layouts and partial refresh for live values.
- Make page transitions visible as a graph before rebuilding screens in LVGL/EEZ Studio.
- Separate UI rendering from hardware/service logic.

## Global Controls

| Button | Global intent | Notes |
| --- | --- | --- |
| P4 | Back / exit current page | Voice call uses P4 to exit call mode. |
| P5 | Previous / decrease / reset | Depends on active page. |
| P6 | Next / increase / toggle | Depends on active page. |
| P7 | OK / confirm / call / calibrate | Depends on active page. |

## Page Graph

```mermaid
flowchart TD
    Home[Watch Face / Home]
    Menu[Menu List]
    Settings[Settings]
    Wifi[Wi-Fi QR Setup]
    Server[Server Status]
    Led[LED Control]
    Voice[AI Call]
    Mpu[MPU DATA]
    Angle[ANGLE]
    Level[LEVEL]
    Odo[ODO]
    Event[EVENT]

    Home -- P4 or P6: menu --> Menu
    Home -- P5: server shortcut --> Server
    Home -- P7: AI call shortcut --> Voice

    Menu -- P5: previous --> Menu
    Menu -- P6: next --> Menu
    Menu -- P7: open SET --> Settings
    Menu -- P7: open Server --> Server
    Menu -- P7: open LED --> Led
    Menu -- P7: open Voice --> Voice
    Menu -- P7: open MPU DATA --> Mpu
    Menu -- P7: open ANGLE --> Angle
    Menu -- P7: open LEVEL --> Level
    Menu -- P7: open ODO --> Odo
    Menu -- P7: open EVENT --> Event
    Menu -- P4: back --> Home

    Settings -- P7: Wi-Fi setup --> Wifi
    Wifi -- P4: back --> Settings

    Server -- P4: back --> Home
    Led -- P4: off/back --> Home
    Voice -- P4: exit call --> Home
    Mpu -- P4: back --> Home
    Angle -- P4: back --> Home
    Level -- P4: back --> Home
    Odo -- P4: back --> Home
    Event -- P4: back --> Home
```

## Button Logic Graph

```mermaid
flowchart LR
    P4((P4))
    P5((P5))
    P6((P6))
    P7((P7))

    Menu[Menu]
    Settings[Settings]
    Voice[AI Call]
    Led[LED]
    MpuPages[MPU Pages]
    Event[EVENT]
    Odo[ODO]

    P4 -->|Back| Menu
    P4 -->|Exit page| Settings
    P4 -->|Exit call| Voice
    P4 -->|Back| MpuPages

    P5 -->|Previous item| Menu
    P6 -->|Next item| Menu
    P7 -->|Open selected| Menu

    P5 -->|Volume down| Settings
    P6 -->|Volume up| Settings
    P7 -->|Wi-Fi QR| Settings

    P5 -->|Volume down| Voice
    P6 -->|Volume up| Voice
    P7 -->|Start CALL| Voice

    P4 -->|Off/back| Led
    P5 -->|Rainbow| Led
    P6 -->|Breathe| Led
    P7 -->|Flash| Led

    P7 -->|Zero calibration| MpuPages
    P5 -->|Reset steps| Odo
    P5 -->|Clear counts| Event
    P6 -->|MPU LED feedback toggle| Event
```

## Current Pages

| Page | Current render function | Live data | Preferred EEZ screen |
| --- | --- | --- | --- |
| Home | `renderWatchFace()` | time, weather, CPU, memory, Wi-Fi | `screen_home` |
| Menu | `renderMenuPage()` | selected item | `screen_menu` |
| Settings | `renderSettingsPage()` | volume, Wi-Fi, version | `screen_settings` |
| Wi-Fi QR | `renderWifiSetupPage()` | portal AP and QR | `screen_wifi_setup` |
| Server | `renderServerStatus()` | load, memory, disk, uptime | `screen_server` |
| LED | `renderLightPage()` | LED mode | `screen_led` |
| AI Call | `renderVoicePage()` | call state, volume, transcript bitmap | `screen_ai_call` |
| MPU DATA | `renderMpuPage()` | accel, gyro, temperature | `screen_mpu_data` |
| ANGLE | `renderAnglePage()` | roll, pitch, yaw rate | `screen_angle` |
| LEVEL | `renderLevelPage()` | bubble x/y | `screen_level` |
| ODO | `renderOdometerPage()` | steps, distance, motion | `screen_odometer` |
| EVENT | `renderMotionPage()` | state, counts, LED feedback | `screen_motion_event` |

## State Model

The firmware now uses a single page state instead of page booleans:

```cpp
enum class UiPage : uint8_t {
  Watch,
  Menu,
  Voice,
  Server,
  Light,
  Settings,
  WifiSetup,
  MpuData,
  MpuLevel,
  MpuAngle,
  Odometer,
  MpuMotion,
};
```

Recommended shared model variables:

| Variable | Type | Owner |
| --- | --- | --- |
| `currentUiPage` | `UiPage` | UI router |
| `menuSelectedIndex` | `uint8_t` | menu screen |
| `speakerVolumePercent` | `int` | audio/settings |
| `voiceStatus` | enum/string | voice service |
| `voiceCallMode` | bool | voice service |
| `ledMode` | enum | LED service |
| `mpuData` | struct | MPU service |
| `mpuCalibration` | struct | MPU service |
| `weather` | struct | weather service |
| `serverStatus` | struct | server service |

## EEZ Studio Mapping

Use EEZ screens for layout and EEZ Flow actions for navigation.

Suggested EEZ assets:

- `screen_home`: watch face arcs, time labels, weather labels.
- `screen_menu`: list with selected item highlight.
- `screen_ai_call`: call status, mic animation, transcript/reply area, volume indicator.
- `screen_mpu_*`: common top bar and bottom button bar, different content panels.
- `screen_wifi_setup`: QR code object plus AP/IP labels.

Suggested EEZ actions:

| Action | Trigger | Effect |
| --- | --- | --- |
| `go_home` | P4 on most screens | set `uiPage = Home` |
| `menu_prev` | P5 on menu | decrement selected item |
| `menu_next` | P6 on menu | increment selected item |
| `menu_open` | P7 on menu | open selected page |
| `start_call` | P7 on AI page | start voice call mode |
| `stop_call` | P4 on AI page | stop voice call mode |
| `mpu_calibrate` | P7 on MPU pages | save current MPU zero |
| `event_toggle_led` | P6 on EVENT | toggle MPU event LED feedback |

## Refactor Steps

1. Done: add `UiPage` and keep old render functions, replacing page booleans.
2. Done: add a central `dispatchButton(index)` function.
3. In progress: move each page's static layout and dynamic refresh into separate functions.
   - Done: AI call page frame/content split, recording timer now refreshes only the content area.
   - Done: menu page frame/list split, selection changes redraw only affected rows when the window does not scroll.
   - Existing: MPU pages use dynamic refresh helpers for live values.
4. Build EEZ Studio screens matching this document.
5. Export LVGL screen code and connect generated callbacks to existing services.
6. Delete old raw drawing pages after EEZ-generated screens are stable.

## Notes

- The current UI mixes raw ST7735 drawing and LVGL. EEZ migration should target LVGL-only screens where possible.
- MPU pages already use partial refresh logic; preserve that behavior in EEZ by updating labels/widgets rather than rebuilding screens.
- AI call mode is currently half-duplex over HTTP streaming chunks. True full-duplex requires a server-side WebSocket or bidirectional streaming endpoint.
