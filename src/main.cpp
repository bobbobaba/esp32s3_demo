#include <Arduino.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <libb64/cdecode.h>
#include <lvgl.h>
#include <time.h>

#include "mi_style_watchface.h"

namespace {

constexpr char kApName[] = "ESP32S3-Setup";
constexpr char kPortalUrl[] = "http://192.168.4.1/";
constexpr char kPreferencesNamespace[] = "wifi_setup";
constexpr uint16_t kDnsPort = 53;
constexpr int kTftDcPin = 9;
constexpr int kTftCsPin = 10;
constexpr int kTftMosiPin = 11;
constexpr int kTftSclkPin = 12;
constexpr int kTftResetPin = 8;
constexpr int kTftBacklightPin = 15;
constexpr int kTftWidth = 128;
constexpr int kTftHeight = 128;
constexpr int kButtonPins[] = {4, 5, 6, 7};
constexpr int kBoardLedPin = 48;
constexpr int kMpuSdaPin = 42;
constexpr int kMpuSclPin = 41;
constexpr uint8_t kMpuAddress = 0x68;
constexpr unsigned long kButtonDebounceMs = 35;
constexpr int kMicBclkPin = 16;
constexpr int kMicWsPin = 17;
constexpr int kMicDataPin = 18;
constexpr int kSpeakerDataPin = 14;
constexpr int kVoiceSampleRate = 16000;
constexpr int kSpeakerVolumeMinPercent = 80;
constexpr int kSpeakerVolumeMaxPercent = 1200;
constexpr int kSpeakerVolumeStepPercent = 100;
constexpr unsigned long kVoiceMaxRecordMs = 10000;
constexpr unsigned long kVoiceCallMinRecordMs = 800;
constexpr unsigned long kVoiceCallSilenceMs = 950;
constexpr unsigned long kVoiceCallNoSpeechRestartMs = 6500;
constexpr unsigned long kVoiceCallRestartDelayMs = 450;
constexpr int32_t kVoiceSpeechLevel = 650;
constexpr size_t kVoiceMaxPcmBytes = kVoiceSampleRate * 2 * kVoiceMaxRecordMs / 1000;
constexpr size_t kVoiceFallbackPcmBytes = kVoiceSampleRate * 2 * 3;
constexpr size_t kVoiceMinPcmBytes = 16000;
constexpr uint8_t kVoiceReplyBitmapWidth = 128;
constexpr uint8_t kVoiceReplyBitmapHeight = 80;
constexpr size_t kVoiceReplyBitmapBytes = kVoiceReplyBitmapWidth * kVoiceReplyBitmapHeight / 8;
constexpr char kServiceNamespace[] = "dud_service";
#ifndef SERVICE_BASE_URL
#define SERVICE_BASE_URL "http://example.invalid"
#endif
constexpr char kAuthUrl[] = SERVICE_BASE_URL "/api/v1/auth/login";
constexpr char kWeatherUrl[] = SERVICE_BASE_URL "/api/v1/weather?compact=1";
constexpr char kServerStatusUrl[] = SERVICE_BASE_URL "/api/v1/server-status";
constexpr char kDeviceConfigUrl[] = SERVICE_BASE_URL "/api/v1/device-config";
constexpr char kVoiceUploadUrl[] = SERVICE_BASE_URL "/api/v1/ai-call/audio";
constexpr char kVoiceStreamStartUrl[] = SERVICE_BASE_URL "/api/v1/ai-call/stream/start";
constexpr char kVoiceStreamBaseUrl[] = SERVICE_BASE_URL "/api/v1/ai-call/stream/";
constexpr size_t kVoiceStreamChunkPcmBytes = kVoiceSampleRate * 2;
constexpr char kAudioNamespace[] = "audio";
constexpr unsigned long kWeatherRefreshMs = 10UL * 60UL * 1000UL;
constexpr unsigned long kServerRefreshMs = 30UL * 1000UL;
constexpr unsigned long kDashboardPageMs = 8UL * 1000UL;
constexpr unsigned long kVoiceResultHoldMs = 30UL * 1000UL;
constexpr unsigned long kFastRetryMs = 5UL * 1000UL;
constexpr unsigned long kSlowNetworkRetryMs = 2UL * 60UL * 1000UL;
constexpr unsigned long kOtaCheckMs = 5UL * 60UL * 1000UL;
constexpr unsigned long kMpuRefreshMs = 200UL;
constexpr unsigned long kMpuGestureMs = 80UL;
constexpr unsigned long kMpuRetryMs = 2000UL;
constexpr unsigned long kShakeCooldownMs = 1200UL;
constexpr unsigned long kRaiseCooldownMs = 2000UL;
constexpr unsigned long kStillDetectMs = 2500UL;
constexpr unsigned long kFallCooldownMs = 2500UL;
constexpr unsigned long kMpuLedPulseMs = 900UL;
constexpr float kOdometerStepMeters = 0.70f;
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

DNSServer dnsServer;
Preferences preferences;
WebServer server(80);
bool portalActive = false;
bool portalRoutesRegistered = false;

struct WeatherData {
  bool valid = false;
  float temperature = NAN;
  float apparentTemperature = NAN;
  int humidity = -1;
  int weatherCode = -1;
  float windSpeed = NAN;
  float precipitation = NAN;
  String updatedAt;
};

struct ServerStatus {
  bool valid = false;
  int uptimeSeconds = 0;
  int cpuCount = 0;
  float load1m = NAN;
  float memoryUsedPercent = NAN;
  float diskUsedPercent = NAN;
  String sampledAt;
};

struct MpuData {
  bool valid = false;
  float ax = NAN;
  float ay = NAN;
  float az = NAN;
  float gx = NAN;
  float gy = NAN;
  float gz = NAN;
  float temperature = NAN;
};

struct MpuCalibration {
  bool valid = false;
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
};

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
  MpuOdometer,
  MpuMotion,
};

enum class VoiceRenderMode : uint8_t {
  None,
  Uploading,
  Recording,
  Speaking,
  Result,
  Idle,
};

WeatherData weather;
ServerStatus serverStatus;
MpuData mpuData;
MpuCalibration mpuCalibration;
String accessToken;
unsigned long nextWeatherFetchMs = 0;
unsigned long nextServerFetchMs = 0;
unsigned long nextDashboardPageMs = 0;
unsigned long nextOtaCheckMs = 30000;
unsigned long nextWatchFaceRefreshMs = 0;
unsigned long nextMpuRefreshMs = 0;
unsigned long nextMpuGestureMs = 0;
unsigned long nextMpuRetryMs = 0;
unsigned long lastShakeMs = 0;
unsigned long lastRaiseMs = 0;
unsigned long lastOdometerStepMs = 0;
unsigned long lastMotionMs = 0;
unsigned long lastFallMs = 0;
unsigned long mpuLedPulseUntilMs = 0;
UiPage currentUiPage = UiPage::Watch;
uint8_t menuSelectedIndex = 0;
uint8_t lastMenuRenderedIndex = 0xFF;
uint8_t lastMenuRenderedStart = 0xFF;
bool autoRotatePages = false;
bool buttonRawState[4] = {HIGH, HIGH, HIGH, HIGH};
bool buttonStableState[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long buttonChangedMs[4] = {0, 0, 0, 0};
String weatherStatus = "WAIT WIFI";
bool microphoneDriverInstalled = false;
bool speakerDriverInstalled = false;
bool mpuReady = false;
String mpuGestureStatus = "READY";
uint8_t mpuReadFailures = 0;
bool mpuLedFeedback = false;
float lastMpuAx = NAN;
float lastMpuAy = NAN;
float lastMpuAz = NAN;
float lastOdometerAccel = NAN;
uint32_t odometerSteps = 0;
uint32_t shakeCount = 0;
uint32_t raiseCount = 0;
uint32_t fallCount = 0;
float mpuAccelMagnitude = NAN;
float mpuGyroMagnitude = NAN;
float mpuPeakAccel = 0.0f;
float mpuPeakGyro = 0.0f;
float mpuRollDeg = NAN;
float mpuPitchDeg = NAN;
float mpuYawRateDeg = NAN;
bool mpuStill = false;
bool voiceRecording = false;
bool voiceStopRequested = false;
bool voiceUploadPending = false;
bool voiceCallMode = false;
bool voiceSpeechDetected = false;
bool voiceStreamActive = false;
bool voiceStreamFailed = false;
uint8_t *voiceWav = nullptr;
size_t voicePcmBytes = 0;
size_t voicePcmCapacity = 0;
size_t voiceStreamUploadedBytes = 0;
unsigned long voiceRecordStartedMs = 0;
unsigned long voiceLastSoundMs = 0;
unsigned long voiceCallRestartMs = 0;
unsigned long voiceResultHoldUntilMs = 0;
String voiceStatus = "READY";
String voiceTranscript;
String voiceReply;
String voiceStreamSessionId;
String otaStatus = "OTA OFF";
String otaAvailableVersion;
uint8_t voiceReplyBitmap[kVoiceReplyBitmapBytes] = {};
bool voiceReplyBitmapValid = false;
int speakerVolumePercent = 500;
VoiceRenderMode lastVoiceRenderMode = VoiceRenderMode::None;
String lastVoiceRenderedStatus;
int lastVoiceRenderedVolume = -1;
unsigned long lastVoiceRenderedSecond = static_cast<unsigned long>(-1);
bool settingsPageRendered = false;
int lastSettingsRenderedVolume = -1;
bool lastSettingsRenderedWifiConnected = false;
bool serverPageRendered = false;
bool lastServerRenderedValid = false;
bool lightPageRendered = false;
uint8_t lastLightRenderedMode = 0xFF;
uint8_t lastLightRenderedRed = 0xFF;
uint8_t lastLightRenderedGreen = 0xFF;
uint8_t lastLightRenderedBlue = 0xFF;
bool wifiSetupPageRendered = false;
String lastSettingsRenderedOtaStatus;
uint8_t ledRed = 0;
uint8_t ledGreen = 0;
uint8_t ledBlue = 0;
enum class LedMode : uint8_t {
  Off,
  Rainbow,
  Breathe,
  Flash,
};
LedMode ledMode = LedMode::Off;

enum class MenuItem : uint8_t {
  Settings,
  Server,
  Light,
  Voice,
  Mpu,
  Angle,
  Level,
  Odometer,
  Motion,
};

enum class SensorPageId : uint8_t {
  None,
  Mpu,
  Level,
  Angle,
  Odometer,
  Motion,
};

struct MenuEntry {
  MenuItem item;
  const char *label;
  uint16_t accent;
};

const MenuEntry kMenuEntries[] = {
    {MenuItem::Settings, "SET", 0xFFE0},
    {MenuItem::Server, "服务器", 0x07E0},
    {MenuItem::Light, "LED", 0x07FF},
    {MenuItem::Voice, "语音", 0xF81F},
    {MenuItem::Mpu, "MPU DATA", 0x07FF},
    {MenuItem::Angle, "ANGLE", 0x07E0},
    {MenuItem::Level, "LEVEL", 0xFFE0},
    {MenuItem::Odometer, "ODO", 0xF81F},
    {MenuItem::Motion, "EVENT", 0xFD20},
};
constexpr uint8_t kMenuEntryCount = sizeof(kMenuEntries) / sizeof(kMenuEntries[0]);
unsigned long nextLedEffectMs = 0;
uint16_t ledEffectStep = 0;
SensorPageId lastDynamicSensorPage = SensorPageId::None;
bool lvglReady = false;
bool lvglWatchFaceBuilt = false;
lv_disp_draw_buf_t lvglDrawBuffer;
lv_disp_drv_t lvglDisplayDriver;
lv_color_t lvglBuffer[kTftWidth * 16];
const lv_img_dsc_t kMiStyleLvglImage = {
    {LV_IMG_CF_TRUE_COLOR, 0, 0, kTftWidth, kTftHeight},
    kTftWidth * kTftHeight * 2,
    reinterpret_cast<const uint8_t *>(kMiStyleWatchface),
};
lv_obj_t *watchRoot = nullptr;
lv_obj_t *watchBg = nullptr;
lv_obj_t *watchTimePlate = nullptr;
lv_obj_t *watchInfoPlate = nullptr;
lv_obj_t *watchArcTemp = nullptr;
lv_obj_t *watchArcMem = nullptr;
lv_obj_t *watchLabelHour = nullptr;
lv_obj_t *watchLabelMinute = nullptr;
lv_obj_t *watchLabelDate = nullptr;
lv_obj_t *watchLabelWeather = nullptr;
lv_obj_t *watchLabelNet = nullptr;
lv_obj_t *watchLabelLoad = nullptr;
lv_obj_t *watchLabelMem = nullptr;
lv_obj_t *watchLabelAi = nullptr;
lv_obj_t *watchBarLoad = nullptr;
lv_obj_t *watchBarMem = nullptr;
lv_obj_t *wifiSetupLabelStatus = nullptr;
lv_obj_t *wifiSetupLabelAp = nullptr;
lv_obj_t *wifiSetupLabelHint = nullptr;

void startConfigurationPortal(bool preserveStation = false);
lv_obj_t *createWatchLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);
void lvglSetLabel(lv_obj_t *label, const String &text);
void renderWeather();
bool watchFaceVisible();
bool readMpuData();

bool uiPageIs(UiPage page) {
  return currentUiPage == page;
}

bool sensorPageVisible() {
  return uiPageIs(UiPage::MpuData) || uiPageIs(UiPage::MpuLevel) ||
      uiPageIs(UiPage::MpuAngle) || uiPageIs(UiPage::MpuOdometer) ||
      uiPageIs(UiPage::MpuMotion);
}

void setUiPage(UiPage page) {
  const UiPage previousPage = currentUiPage;
  currentUiPage = page;
  if (!sensorPageVisible()) lastDynamicSensorPage = SensorPageId::None;
  if (previousPage != page || page != UiPage::Voice) {
    lastVoiceRenderMode = VoiceRenderMode::None;
    lastVoiceRenderedStatus = "";
    lastVoiceRenderedVolume = -1;
    lastVoiceRenderedSecond = static_cast<unsigned long>(-1);
  }
  if (previousPage != page || page != UiPage::Menu) {
    lastMenuRenderedIndex = 0xFF;
    lastMenuRenderedStart = 0xFF;
  }
  if (previousPage != page || page != UiPage::Settings) {
    settingsPageRendered = false;
    lastSettingsRenderedVolume = -1;
    lastSettingsRenderedOtaStatus = "";
  }
  if (previousPage != page || page != UiPage::Server) {
    serverPageRendered = false;
  }
  if (previousPage != page || page != UiPage::Light) {
    lightPageRendered = false;
    lastLightRenderedMode = 0xFF;
    lastLightRenderedRed = 0xFF;
    lastLightRenderedGreen = 0xFF;
    lastLightRenderedBlue = 0xFF;
  }
  if (previousPage != page || page != UiPage::WifiSetup) {
    wifiSetupPageRendered = false;
    wifiSetupLabelStatus = nullptr;
    wifiSetupLabelAp = nullptr;
    wifiSetupLabelHint = nullptr;
  }
}

#if defined(PROVISION_SSID) && defined(PROVISION_PASSWORD)
void provisionCredentials() {
  preferences.begin(kPreferencesNamespace, false);
  preferences.putString("ssid", PROVISION_SSID);
  preferences.putString("password", PROVISION_PASSWORD);
  preferences.end();
}
#endif

#if defined(PROVISION_SERVICE_USER) && defined(PROVISION_SERVICE_PASSWORD)
void provisionServiceCredentials() {
  preferences.begin(kServiceNamespace, false);
  preferences.putString("username", PROVISION_SERVICE_USER);
  preferences.putString("password", PROVISION_SERVICE_PASSWORD);
  preferences.end();
}
#endif

const char kSetupPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32-S3 Wi-Fi</title><style>body{margin:0;background:#0b0f16;color:#eef3ff;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}.wrap{max-width:460px;margin:0 auto;padding:18px}.card{background:#151b25;border:1px solid #2b3444;border-radius:14px;padding:16px;margin:12px 0;box-shadow:0 10px 30px #0005}h2{margin:4px 0 12px;font-size:22px}.row{display:flex;justify-content:space-between;gap:12px;margin:8px 0;color:#aeb8ca}.row b{color:#fff;text-align:right;word-break:break-all}label{display:block;margin:12px 0 6px;color:#c8d2e4}input{box-sizing:border-box;width:100%;height:42px;border-radius:10px;border:1px solid #3b4658;background:#080b10;color:#fff;padding:0 12px;font-size:16px}button{width:100%;height:44px;border:0;border-radius:10px;background:#19d3ff;color:#001018;font-weight:700;font-size:16px;margin-top:14px}.danger{background:#ff5d57;color:#fff}.hint{color:#8e9bb1;font-size:13px;line-height:1.5}</style></head><body><div class="wrap"><h2>ESP32-S3 网络设置</h2><div class="card"><div class="row"><span>状态</span><b id="sta">读取中</b></div><div class="row"><span>当前 Wi-Fi</span><b id="ssid">--</b></div><div class="row"><span>设备 IP</span><b id="ip">--</b></div><div class="row"><span>配置热点</span><b id="ap">--</b></div><div class="row"><span>热点 IP</span><b id="apip">--</b></div></div><div class="card"><form action="/save" method="post"><label>新的 Wi-Fi 名称</label><input name="ssid" maxlength="32" required autocomplete="off"><label>密码</label><input name="password" type="password" maxlength="64" autocomplete="off"><button type="submit">保存并连接</button></form><p class="hint">手机先连 ESP32S3-Setup 热点，再打开本页。保存后设备会重启连接新 Wi-Fi。</p></div><div class="card"><form action="/reset" method="post"><button class="danger" type="submit">清除已保存 Wi-Fi</button></form></div></div><script>async function load(){try{const r=await fetch('/status');const s=await r.json();sta.textContent=s.connected?'已连接':'未连接';ssid.textContent=s.ssid||'--';ip.textContent=s.ip||'--';ap.textContent=s.ap||'--';apip.textContent=s.ap_ip||'--'}catch(e){sta.textContent='离线'}}load();setInterval(load,3000);</script></body></html>
)HTML";

void tftCommand(uint8_t command) {
  digitalWrite(kTftDcPin, LOW);
  digitalWrite(kTftCsPin, LOW);
  SPI.transfer(command);
  digitalWrite(kTftCsPin, HIGH);
}

void tftData(uint8_t data) {
  digitalWrite(kTftDcPin, HIGH);
  digitalWrite(kTftCsPin, LOW);
  SPI.transfer(data);
  digitalWrite(kTftCsPin, HIGH);
}

void tftSetWindow(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  // This 128x128 ST7735 panel has a 2-column, 3-row controller offset.
  const uint8_t x0 = x + 2;
  const uint8_t y0 = y + 3;
  const uint8_t x1 = x0 + width - 1;
  const uint8_t y1 = y0 + height - 1;
  tftCommand(0x2A);
  tftData(0);
  tftData(x0);
  tftData(0);
  tftData(x1);
  tftCommand(0x2B);
  tftData(0);
  tftData(y0);
  tftData(0);
  tftData(y1);
  tftCommand(0x2C);
}

void tftFillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint16_t color) {
  tftSetWindow(x, y, width, height);
  digitalWrite(kTftDcPin, HIGH);
  digitalWrite(kTftCsPin, LOW);
  for (uint16_t pixel = 0; pixel < static_cast<uint16_t>(width) * height; ++pixel) {
    SPI.transfer(color >> 8);
    SPI.transfer(color & 0xFF);
  }
  digitalWrite(kTftCsPin, HIGH);
}

void tftDrawRgb565Image(const uint16_t *image) {
  tftSetWindow(0, 0, kTftWidth, kTftHeight);
  digitalWrite(kTftDcPin, HIGH);
  digitalWrite(kTftCsPin, LOW);
  for (uint16_t pixel = 0; pixel < static_cast<uint16_t>(kTftWidth) * kTftHeight; ++pixel) {
    const uint16_t color = pgm_read_word(image + pixel);
    SPI.transfer(color >> 8);
    SPI.transfer(color & 0xFF);
  }
  digitalWrite(kTftCsPin, HIGH);
}

void initializeDisplay() {
  pinMode(kTftCsPin, OUTPUT);
  pinMode(kTftDcPin, OUTPUT);
  pinMode(kTftBacklightPin, OUTPUT);
  pinMode(kTftResetPin, OUTPUT);
  digitalWrite(kTftCsPin, HIGH);
  digitalWrite(kTftBacklightPin, HIGH);
  digitalWrite(kTftResetPin, HIGH);
  delay(20);
  digitalWrite(kTftResetPin, LOW);
  delay(40);
  digitalWrite(kTftResetPin, HIGH);
  delay(150);
  SPI.begin(kTftSclkPin, -1, kTftMosiPin, kTftCsPin);
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  tftCommand(0x01);
  delay(150);
  tftCommand(0x11);
  delay(150);
  tftCommand(0x3A);
  tftData(0x05);
  tftCommand(0x36);
  tftData(0x88);
  tftCommand(0x20);
  tftCommand(0x13);
  tftCommand(0x29);
  delay(100);
}

void lvglFlush(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t *colors) {
  int16_t x1 = area->x1 < 0 ? 0 : area->x1;
  int16_t y1 = area->y1 < 0 ? 0 : area->y1;
  int16_t x2 = area->x2 >= kTftWidth ? kTftWidth - 1 : area->x2;
  int16_t y2 = area->y2 >= kTftHeight ? kTftHeight - 1 : area->y2;
  if (x2 < x1 || y2 < y1) {
    lv_disp_flush_ready(display);
    return;
  }

  const uint16_t fullWidth = area->x2 - area->x1 + 1;
  colors += (y1 - area->y1) * fullWidth + (x1 - area->x1);
  const int16_t physicalX1 = x1;
  const int16_t physicalX2 = x2;
  const int16_t physicalY1 = kTftHeight - 1 - y2;
  const int16_t physicalY2 = kTftHeight - 1 - y1;
  tftSetWindow(physicalX1, physicalY1, physicalX2 - physicalX1 + 1, physicalY2 - physicalY1 + 1);
  digitalWrite(kTftDcPin, HIGH);
  digitalWrite(kTftCsPin, LOW);
  for (int16_t y = y2; y >= y1; --y) {
    lv_color_t *row = colors + (y - y1) * fullWidth;
    for (int16_t x = x1; x <= x2; ++x) {
      const uint16_t color = row[x - x1].full;
      SPI.transfer(color >> 8);
      SPI.transfer(color & 0xFF);
    }
  }
  digitalWrite(kTftCsPin, HIGH);
  lv_disp_flush_ready(display);
}

void initializeLvglDisplay() {
  lv_init();
  lv_disp_draw_buf_init(&lvglDrawBuffer, lvglBuffer, nullptr, sizeof(lvglBuffer) / sizeof(lvglBuffer[0]));
  lv_disp_drv_init(&lvglDisplayDriver);
  lvglDisplayDriver.hor_res = kTftWidth;
  lvglDisplayDriver.ver_res = kTftHeight;
  lvglDisplayDriver.flush_cb = lvglFlush;
  lvglDisplayDriver.draw_buf = &lvglDrawBuffer;
  lv_disp_drv_register(&lvglDisplayDriver);
  lvglReady = true;
}

void initializeButtons() {
  for (size_t index = 0; index < 4; ++index) {
    pinMode(kButtonPins[index], INPUT_PULLUP);
    buttonRawState[index] = digitalRead(kButtonPins[index]);
    buttonStableState[index] = buttonRawState[index];
  }
}

void applyBoardLed(uint8_t red, uint8_t green, uint8_t blue) {
  ledRed = red;
  ledGreen = green;
  ledBlue = blue;
  neopixelWrite(kBoardLedPin, red, green, blue);
}

void setLedMode(LedMode mode) {
  ledMode = mode;
  ledEffectStep = 0;
  nextLedEffectMs = 0;
  if (mode == LedMode::Off) applyBoardLed(0, 0, 0);
}

void loadMpuSettings() {
  preferences.begin("mpu6050", true);
  mpuLedFeedback = preferences.getBool("led", false);
  preferences.end();
}

void saveMpuSettings() {
  preferences.begin("mpu6050", false);
  preferences.putBool("led", mpuLedFeedback);
  preferences.end();
}

void pulseMpuLedFeedback() {
  if (!mpuLedFeedback || uiPageIs(UiPage::Light)) return;
  setLedMode(LedMode::Flash);
  mpuLedPulseUntilMs = millis() + kMpuLedPulseMs;
}

void colorWheel(uint8_t position, uint8_t brightness, uint8_t &red, uint8_t &green, uint8_t &blue) {
  position = 255 - position;
  if (position < 85) {
    red = 255 - position * 3;
    green = 0;
    blue = position * 3;
  } else if (position < 170) {
    position -= 85;
    red = 0;
    green = position * 3;
    blue = 255 - position * 3;
  } else {
    position -= 170;
    red = position * 3;
    green = 255 - position * 3;
    blue = 0;
  }
  red = static_cast<uint16_t>(red) * brightness / 255;
  green = static_cast<uint16_t>(green) * brightness / 255;
  blue = static_cast<uint16_t>(blue) * brightness / 255;
}

void serviceBoardLedEffects() {
  if (mpuLedPulseUntilMs != 0 && static_cast<long>(millis() - mpuLedPulseUntilMs) >= 0) {
    mpuLedPulseUntilMs = 0;
    if (!uiPageIs(UiPage::Light) && ledMode == LedMode::Flash) setLedMode(LedMode::Off);
  }
  if (ledMode == LedMode::Off || millis() < nextLedEffectMs) return;
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  if (ledMode == LedMode::Rainbow) {
    colorWheel(ledEffectStep & 0xFF, 70, red, green, blue);
    nextLedEffectMs = millis() + 35;
  } else if (ledMode == LedMode::Breathe) {
    const uint8_t phase = ledEffectStep & 0x7F;
    const uint8_t brightness = phase < 64 ? phase * 2 : (127 - phase) * 2;
    red = static_cast<uint16_t>(0) * brightness / 128;
    green = static_cast<uint16_t>(90) * brightness / 128;
    blue = static_cast<uint16_t>(255) * brightness / 128;
    nextLedEffectMs = millis() + 25;
  } else if (ledMode == LedMode::Flash) {
    const bool redPhase = (ledEffectStep / 5) % 2 == 0;
    red = redPhase ? 100 : 0;
    blue = redPhase ? 0 : 100;
    nextLedEffectMs = millis() + 45;
  }
  ++ledEffectStep;
  applyBoardLed(red, green, blue);
  if (uiPageIs(UiPage::Light)) renderWeather();
}

void initializeBoardLed() {
  pinMode(kBoardLedPin, OUTPUT);
  setLedMode(LedMode::Off);
}

const uint8_t *glyphFor(char character) {
  static const uint8_t blank[5] = {0, 0, 0, 0, 0};
  static const uint8_t zero[5] = {0x3E, 0x45, 0x49, 0x51, 0x3E};
  static const uint8_t one[5] = {0x00, 0x21, 0x7F, 0x01, 0x00};
  static const uint8_t two[5] = {0x23, 0x45, 0x49, 0x51, 0x21};
  static const uint8_t three[5] = {0x42, 0x41, 0x51, 0x69, 0x46};
  static const uint8_t four[5] = {0x0C, 0x14, 0x24, 0x7F, 0x04};
  static const uint8_t five[5] = {0x72, 0x51, 0x51, 0x51, 0x4E};
  static const uint8_t six[5] = {0x1E, 0x29, 0x49, 0x49, 0x06};
  static const uint8_t seven[5] = {0x40, 0x47, 0x48, 0x50, 0x60};
  static const uint8_t eight[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t nine[5] = {0x30, 0x49, 0x49, 0x4A, 0x3C};
  static const uint8_t a[5] = {0x3F, 0x44, 0x44, 0x44, 0x3F};
  static const uint8_t c[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
  static const uint8_t e[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
  static const uint8_t f[5] = {0x7F, 0x48, 0x48, 0x48, 0x40};
  static const uint8_t h[5] = {0x7F, 0x08, 0x04, 0x04, 0x78};
  static const uint8_t i[5] = {0x00, 0x41, 0x7D, 0x41, 0x00};
  static const uint8_t k[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
  static const uint8_t d[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
  static const uint8_t l[5] = {0x7F, 0x01, 0x01, 0x01, 0x01};
  static const uint8_t m[5] = {0x7F, 0x20, 0x10, 0x20, 0x7F};
  static const uint8_t n[5] = {0x7F, 0x08, 0x04, 0x04, 0x78};
  static const uint8_t o[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
  static const uint8_t p[5] = {0x7F, 0x48, 0x48, 0x48, 0x30};
  static const uint8_t r[5] = {0x7F, 0x48, 0x4C, 0x4A, 0x31};
  static const uint8_t s[5] = {0x31, 0x49, 0x49, 0x49, 0x46};
  static const uint8_t t[5] = {0x40, 0x40, 0x7F, 0x40, 0x40};
  static const uint8_t u[5] = {0x7C, 0x02, 0x01, 0x02, 0x7C};
  static const uint8_t v[5] = {0x70, 0x0C, 0x03, 0x0C, 0x70};
  static const uint8_t w[5] = {0x7C, 0x02, 0x1C, 0x02, 0x7C};
  static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static const uint8_t period[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static const uint8_t percent[5] = {0x63, 0x64, 0x08, 0x13, 0x63};
  switch (character) {
    case '0': return zero; case '1': return one; case '2': return two;
    case '3': return three; case '4': return four; case '5': return five;
    case '6': return six; case '7': return seven; case '8': return eight;
    case '9': return nine; case 'A': return a; case 'C': return c;
    case 'E': return e; case 'F': return f; case 'H': return h;
    case 'D': return d; case 'I': return i; case 'K': return k;
    case 'L': return l; case 'M': return m; case 'N': return n;
    case 'O': return o; case 'P': return p; case 'R': return r;
    case 'S': return s; case 'T': return t; case 'U': return u;
    case 'V': return v; case 'W': return w;
    case '-': return dash; case '.': return period; case ':': return colon;
    case '%': return percent; default: return blank;
  }
}

void drawText(uint8_t x, uint8_t y, const String &text, uint16_t color, uint8_t scale = 1) {
  for (size_t index = 0; index < text.length(); ++index) {
    const uint8_t *glyph = glyphFor(text[index]);
    for (uint8_t column = 0; column < 5; ++column) {
      for (uint8_t row = 0; row < 7; ++row) {
        if (glyph[column] & (1 << row)) {
          tftFillRect(x + column * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += 6 * scale;
    if (x + 5 * scale >= kTftWidth) return;
  }
}

String jsonString(const String &body, const char *key, const String &fallback = "") {
  const String marker = String("\"") + key + "\"";
  int position = body.indexOf(marker);
  if (position < 0) return fallback;
  position = body.indexOf(':', position + marker.length());
  if (position < 0) return fallback;
  while (++position < body.length() && isspace(body[position])) {}
  if (position >= body.length() || body[position] != '\"') return fallback;
  const int end = body.indexOf('\"', position + 1);
  return end < 0 ? fallback : body.substring(position + 1, end);
}

float jsonNumber(const String &body, const char *key, float fallback = NAN) {
  const String marker = String("\"") + key + "\"";
  int position = body.indexOf(marker);
  if (position < 0) return fallback;
  position = body.indexOf(':', position + marker.length());
  if (position < 0) return fallback;
  while (++position < body.length() && isspace(body[position])) {}
  int end = position;
  while (end < body.length() && (isDigit(body[end]) || body[end] == '-' || body[end] == '+' || body[end] == '.')) ++end;
  const String value = body.substring(position, end);
  return value.length() == 0 ? fallback : value.toFloat();
}

String jsonObject(const String &body, const char *key) {
  const String marker = String("\"") + key + "\"";
  int position = body.indexOf(marker);
  if (position < 0) return "";
  position = body.indexOf('{', position + marker.length());
  if (position < 0) return "";
  int depth = 0;
  for (int index = position; index < body.length(); ++index) {
    if (body[index] == '{') ++depth;
    if (body[index] == '}' && --depth == 0) return body.substring(position, index + 1);
  }
  return "";
}

bool decodeVoiceReplyBitmap(const String &encoded) {
  if (encoded.isEmpty() || encoded.length() > 1800) return false;
  const int decoded = base64_decode_chars(encoded.c_str(), encoded.length(),
      reinterpret_cast<char *>(voiceReplyBitmap));
  return decoded == static_cast<int>(kVoiceReplyBitmapBytes);
}

bool voiceResultHeld() {
  return voiceResultHoldUntilMs != 0 &&
      static_cast<long>(voiceResultHoldUntilMs - millis()) > 0;
}

String jsonEscape(const String &value) {
  String escaped;
  for (size_t index = 0; index < value.length(); ++index) {
    if (value[index] == '\"' || value[index] == '\\') escaped += '\\';
    escaped += value[index];
  }
  return escaped;
}

String displayNumber(float value) {
  return isfinite(value) ? String(value, 1) : "--";
}

float mpuAxZeroed() {
  return mpuData.ax - (mpuCalibration.valid ? mpuCalibration.ax : 0.0f);
}

float mpuAyZeroed() {
  return mpuData.ay - (mpuCalibration.valid ? mpuCalibration.ay : 0.0f);
}

float mpuAzZeroed() {
  return mpuData.az - (mpuCalibration.valid ? mpuCalibration.az : 0.0f) + 1.0f;
}

float mpuGxZeroed() {
  return mpuData.gx - (mpuCalibration.valid ? mpuCalibration.gx : 0.0f);
}

float mpuGyZeroed() {
  return mpuData.gy - (mpuCalibration.valid ? mpuCalibration.gy : 0.0f);
}

float mpuGzZeroed() {
  return mpuData.gz - (mpuCalibration.valid ? mpuCalibration.gz : 0.0f);
}

void updateMpuDerivedState() {
  if (!mpuData.valid) return;
  const float ax = mpuAxZeroed();
  const float ay = mpuAyZeroed();
  const float az = mpuAzZeroed();
  const float gx = mpuGxZeroed();
  const float gy = mpuGyZeroed();
  const float gz = mpuGzZeroed();
  mpuAccelMagnitude = sqrtf(ax * ax + ay * ay + az * az);
  mpuGyroMagnitude = sqrtf(gx * gx + gy * gy + gz * gz);
  mpuPeakAccel = max(mpuPeakAccel, mpuAccelMagnitude);
  mpuPeakGyro = max(mpuPeakGyro, mpuGyroMagnitude);
  mpuRollDeg = atan2f(ay, az) * 180.0f / PI;
  mpuPitchDeg = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;
  mpuYawRateDeg = gz;
}

void loadMpuCalibration() {
  preferences.begin("mpu6050", true);
  mpuCalibration.valid = preferences.getBool("cal", false);
  mpuCalibration.ax = preferences.getFloat("ax", 0.0f);
  mpuCalibration.ay = preferences.getFloat("ay", 0.0f);
  mpuCalibration.az = preferences.getFloat("az", 0.0f);
  mpuCalibration.gx = preferences.getFloat("gx", 0.0f);
  mpuCalibration.gy = preferences.getFloat("gy", 0.0f);
  mpuCalibration.gz = preferences.getFloat("gz", 0.0f);
  preferences.end();
}

void saveMpuCalibration() {
  preferences.begin("mpu6050", false);
  preferences.putBool("cal", mpuCalibration.valid);
  preferences.putFloat("ax", mpuCalibration.ax);
  preferences.putFloat("ay", mpuCalibration.ay);
  preferences.putFloat("az", mpuCalibration.az);
  preferences.putFloat("gx", mpuCalibration.gx);
  preferences.putFloat("gy", mpuCalibration.gy);
  preferences.putFloat("gz", mpuCalibration.gz);
  preferences.end();
}

void calibrateMpuZero() {
  if (!readMpuData()) return;
  mpuCalibration.valid = true;
  mpuCalibration.ax = mpuData.ax;
  mpuCalibration.ay = mpuData.ay;
  mpuCalibration.az = mpuData.az;
  mpuCalibration.gx = mpuData.gx;
  mpuCalibration.gy = mpuData.gy;
  mpuCalibration.gz = mpuData.gz;
  saveMpuCalibration();
  lastMpuAx = NAN;
  lastMpuAy = NAN;
  lastMpuAz = NAN;
  lastOdometerAccel = NAN;
  mpuPeakAccel = 0.0f;
  mpuPeakGyro = 0.0f;
  mpuGestureStatus = "CAL";
}

bool mpuWriteRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kMpuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void markMpuReadFailed() {
  mpuData.valid = false;
  if (mpuReadFailures < 255) ++mpuReadFailures;
  if (mpuReadFailures >= 2) {
    mpuReady = false;
    mpuGestureStatus = "OFF";
    nextMpuRetryMs = millis() + kMpuRetryMs;
  }
}

bool readMpuData() {
  if (!mpuReady) return false;
  Wire.beginTransmission(kMpuAddress);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) {
    markMpuReadFailed();
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(kMpuAddress), 14) != 14) {
    markMpuReadFailed();
    return false;
  }

  auto read16 = []() -> int16_t {
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    return static_cast<int16_t>((high << 8) | low);
  };

  const int16_t rawAx = read16();
  const int16_t rawAy = read16();
  const int16_t rawAz = read16();
  const int16_t rawTemp = read16();
  const int16_t rawGx = read16();
  const int16_t rawGy = read16();
  const int16_t rawGz = read16();

  mpuData.ax = rawAx / 16384.0f;
  mpuData.ay = rawAy / 16384.0f;
  mpuData.az = rawAz / 16384.0f;
  mpuData.temperature = rawTemp / 340.0f + 36.53f;
  mpuData.gx = rawGx / 131.0f;
  mpuData.gy = rawGy / 131.0f;
  mpuData.gz = rawGz / 131.0f;
  mpuData.valid = true;
  mpuReadFailures = 0;
  updateMpuDerivedState();
  return true;
}

void wakeDisplayFromGesture() {
  digitalWrite(kTftBacklightPin, HIGH);
  nextWatchFaceRefreshMs = 0;
  if (watchFaceVisible()) renderWeather();
}

void serviceMpuGestures() {
  if (!mpuReady || !mpuData.valid) return;
  const unsigned long now = millis();
  const bool hasLast = isfinite(lastMpuAx) && isfinite(lastMpuAy) && isfinite(lastMpuAz);
  const float accelDelta = hasLast ?
      fabsf(mpuData.ax - lastMpuAx) + fabsf(mpuData.ay - lastMpuAy) + fabsf(mpuData.az - lastMpuAz) : 0.0f;
  const float gyroMotion = fabsf(mpuGxZeroed()) + fabsf(mpuGyZeroed()) + fabsf(mpuGzZeroed());
  const bool shake = accelDelta > 1.25f && gyroMotion > 120.0f &&
      static_cast<long>(now - lastShakeMs) > static_cast<long>(kShakeCooldownMs);
  const bool raise = mpuData.az > 0.55f && fabsf(mpuData.ax) < 0.85f && gyroMotion > 35.0f &&
      static_cast<long>(now - lastRaiseMs) > static_cast<long>(kRaiseCooldownMs);
  const bool freeFall = isfinite(mpuAccelMagnitude) && mpuAccelMagnitude < 0.35f;
  const bool impact = isfinite(mpuAccelMagnitude) && mpuAccelMagnitude > 2.45f;
  const bool fall = (freeFall || impact) &&
      static_cast<long>(now - lastFallMs) > static_cast<long>(kFallCooldownMs);
  if (accelDelta > 0.08f || gyroMotion > 18.0f) lastMotionMs = now;
  mpuStill = static_cast<long>(now - lastMotionMs) > static_cast<long>(kStillDetectMs);

  lastMpuAx = mpuData.ax;
  lastMpuAy = mpuData.ay;
  lastMpuAz = mpuData.az;

  if (shake) {
    lastShakeMs = now;
    ++shakeCount;
    mpuGestureStatus = "SHAKE";
    pulseMpuLedFeedback();
    if (uiPageIs(UiPage::MpuData) || uiPageIs(UiPage::MpuMotion)) renderWeather();
  } else if (raise) {
    lastRaiseMs = now;
    ++raiseCount;
    mpuGestureStatus = "RAISE";
    wakeDisplayFromGesture();
    if (uiPageIs(UiPage::MpuData) || uiPageIs(UiPage::MpuMotion)) renderWeather();
  } else if (fall) {
    lastFallMs = now;
    ++fallCount;
    mpuGestureStatus = "FALL?";
    pulseMpuLedFeedback();
    if (uiPageIs(UiPage::MpuMotion)) renderWeather();
  } else if (mpuStill) {
    mpuGestureStatus = "STILL";
  }
}

void serviceOdometer() {
  if (!mpuReady || !mpuData.valid) return;
  const float ax = mpuAxZeroed();
  const float ay = mpuAyZeroed();
  const float az = mpuAzZeroed();
  const float accel = sqrtf(ax * ax + ay * ay + az * az);
  const bool hasLast = isfinite(lastOdometerAccel);
  const float delta = hasLast ? fabsf(accel - lastOdometerAccel) : 0.0f;
  const unsigned long now = millis();
  if (hasLast && accel > 1.18f && delta > 0.18f &&
      static_cast<long>(now - lastOdometerStepMs) > 320) {
    ++odometerSteps;
    lastOdometerStepMs = now;
  }
  lastOdometerAccel = accel;
}

void initializeMpu6050() {
  loadMpuCalibration();
  loadMpuSettings();
  Wire.begin(kMpuSdaPin, kMpuSclPin);
  Wire.setTimeOut(20);
  Wire.setClock(400000);
  mpuReady = mpuWriteRegister(0x6B, 0x00);
  if (mpuReady) {
    mpuReadFailures = 0;
    mpuWriteRegister(0x1C, 0x00);
    mpuWriteRegister(0x1B, 0x00);
    readMpuData();
  } else {
    mpuData.valid = false;
    mpuGestureStatus = "OFF";
    nextMpuRetryMs = millis() + kMpuRetryMs;
  }
}

void retryMpu6050() {
  if (mpuReady || millis() < nextMpuRetryMs) return;
  Wire.setTimeOut(20);
  mpuReady = mpuWriteRegister(0x6B, 0x00);
  if (mpuReady) {
    mpuReadFailures = 0;
    mpuWriteRegister(0x1C, 0x00);
    mpuWriteRegister(0x1B, 0x00);
    mpuGestureStatus = readMpuData() ? "READY" : "OFF";
  } else {
    mpuData.valid = false;
    mpuGestureStatus = "OFF";
    nextMpuRetryMs = millis() + kMpuRetryMs;
  }
}

String updateTimeText() {
  if (weather.updatedAt.length() >= 5) return weather.updatedAt.substring(weather.updatedAt.length() - 5);
  return "--:--";
}

String uptimeText(int seconds) {
  const int days = seconds / 86400;
  const int hours = (seconds % 86400) / 3600;
  const int minutes = (seconds % 3600) / 60;
  return String(days) + "D " + String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes);
}

String clockText() {
  struct tm now = {};
  if (!getLocalTime(&now, 10)) return "--:--";
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", now.tm_hour, now.tm_min);
  return String(buffer);
}

String dateText() {
  struct tm now = {};
  if (!getLocalTime(&now, 10)) return "--.--";
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d-%02d", now.tm_mon + 1, now.tm_mday);
  return String(buffer);
}

struct ZhGlyph {
  const char *utf8;
  uint8_t bits[32];
};

const ZhGlyph kZhGlyphs[] = {
  {"天", {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0xE0,0x04,0x00,0x04,0x00,0x04,0x00,0x7F,0xE0,0x06,0x00,0x06,0x00,0x0D,0x00,0x19,0x80,0x30,0xC0,0x40,0x20,0x00,0x00}},
  {"气", {0x00,0x00,0x00,0x00,0x10,0x00,0x3F,0xE0,0x20,0x00,0x5F,0xC0,0x40,0x00,0x3F,0x80,0x00,0x80,0x00,0x80,0x00,0x80,0x00,0x40,0x00,0x60,0x00,0x20,0x00,0x00,0x00,0x00}},
  {"温", {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2F,0xC0,0x08,0x40,0x0F,0xC0,0x48,0x40,0x2F,0xC0,0x00,0x00,0x0F,0xE0,0x2A,0xA0,0x2A,0xA0,0x4A,0xA0,0x5F,0xF0,0x00,0x00}},
  {"度", {0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x7F,0xE0,0x40,0x80,0x48,0x80,0x5F,0xE0,0x48,0x80,0x47,0x80,0x5F,0xC0,0x48,0x40,0x47,0x80,0x47,0x80,0x18,0x60,0x00,0x00}},
  {"体", {0x00,0x00,0x00,0x00,0x00,0x00,0x11,0x00,0x11,0x00,0x21,0x00,0x2F,0xE0,0x63,0x80,0xE3,0x80,0x25,0x40,0x25,0x40,0x29,0x20,0x37,0xF0,0x21,0x00,0x21,0x00,0x00,0x00}},
  {"感", {0x00,0x00,0x00,0x00,0x00,0xC0,0x3F,0xE0,0x41,0x00,0x5D,0x40,0x5D,0x40,0x54,0x80,0x5D,0xA0,0x42,0x60,0x06,0x40,0x32,0x40,0x50,0xA0,0x0F,0x00,0x00,0x00,0x00,0x00}},
  {"湿", {0x00,0x00,0x00,0x00,0x6F,0xE0,0x38,0x20,0x08,0x20,0x0F,0xE0,0xC8,0x20,0x2F,0xE0,0x02,0x80,0x2A,0xA0,0x2A,0xA0,0x26,0xC0,0x42,0x80,0x5F,0xF0,0x00,0x00,0x00,0x00}},
  {"码", {0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0xC0,0x20,0x40,0x42,0x40,0x72,0x40,0x54,0x40,0xD3,0xE0,0x50,0x20,0x57,0xA0,0x50,0x20,0x60,0x20,0x00,0xC0,0x00,0x00,0x00,0x00}},
  {"风", {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0xC0,0x20,0x40,0x21,0x40,0x29,0x40,0x26,0x40,0x26,0x40,0x26,0x40,0x2D,0x40,0x59,0xC0,0x50,0x50,0x40,0x20,0x00,0x00}},
  {"速", {0x00,0x00,0x00,0x00,0x00,0x00,0x41,0x00,0x2F,0xE0,0x21,0x00,0x0F,0xE0,0x09,0x20,0x69,0x20,0x2F,0xE0,0x23,0x80,0x25,0x40,0x29,0x20,0x71,0x00,0x4F,0xE0,0x00,0x00}},
  {"降", {0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x73,0xE0,0x56,0x40,0x5D,0xC0,0x61,0x80,0x53,0x60,0x54,0x80,0x57,0xE0,0x72,0x80,0x4F,0xE0,0x40,0x80,0x40,0x80,0x00,0x00}},
  {"水", {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x02,0x20,0x7A,0x40,0x0A,0x80,0x15,0x00,0x15,0x00,0x34,0x80,0x24,0xC0,0x44,0x60,0x04,0x20,0x0C,0x00,0x00,0x00}},
  {"更", {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0xE0,0x02,0x00,0x3F,0xC0,0x22,0x40,0x3F,0xC0,0x22,0x40,0x3F,0xC0,0x24,0x00,0x1C,0x00,0x1E,0x00,0x61,0xE0,0x00,0x00}},
  {"新", {0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x20,0x7D,0xC0,0x0A,0x00,0x2A,0x00,0x7D,0xF0,0x12,0x40,0x7E,0x40,0x32,0x40,0x3A,0x40,0x52,0x40,0x16,0x40,0x14,0x40,0x00,0x00}},
  {"连", {0x00,0x00,0x00,0x00,0x42,0x00,0x2F,0xE0,0x04,0x00,0x05,0x00,0x6F,0xE0,0x21,0x00,0x21,0x00,0x2F,0xE0,0x21,0x00,0x21,0x00,0x51,0x00,0x8F,0xE0,0x00,0x00,0x00,0x00}},
  {"接", {0x00,0x00,0x00,0x00,0x21,0x00,0x2F,0xE0,0x22,0x40,0x72,0x40,0x2F,0xF0,0x21,0x00,0x32,0x00,0xEF,0xE0,0x24,0x40,0x26,0x80,0x21,0xC0,0x6E,0x20,0x00,0x00,0x00,0x00}},
  {"失", {0x00,0x00,0x00,0x00,0x14,0x00,0x24,0x00,0x3F,0xC0,0x24,0x00,0x44,0x00,0x04,0x00,0x7F,0xE0,0x06,0x00,0x0D,0x00,0x19,0x00,0x30,0xC0,0x40,0x20,0x00,0x00,0x00,0x00}},
  {"败", {0x00,0x00,0x00,0x00,0x79,0x00,0x49,0x00,0x59,0xE0,0x5A,0x40,0x5A,0x40,0x5E,0x40,0x59,0x40,0x59,0x40,0x20,0x80,0x28,0xC0,0x49,0x60,0x42,0x20,0x00,0x00,0x00,0x00}},
  {"服", {0x00,0x00,0x00,0x00,0x00,0x00,0x7B,0xE0,0x4A,0x20,0x4A,0x20,0x7A,0xE0,0x4A,0x00,0x4B,0xE0,0x4B,0x20,0x7B,0x20,0x4A,0xC0,0x4A,0xC0,0x4A,0xC0,0x9B,0x20,0x00,0x00}},
  {"务", {0x00,0x00,0x00,0x00,0x08,0x00,0x1F,0xC0,0x30,0xC0,0x49,0x80,0x06,0x00,0x79,0xE0,0x04,0x00,0x7F,0xC0,0x04,0x40,0x08,0x40,0x10,0x40,0x61,0x80,0x00,0x00,0x00,0x00}},
  {"器", {0x00,0x00,0x00,0x00,0x00,0x00,0x79,0xE0,0x4A,0x20,0x4A,0x20,0x79,0xE0,0x04,0x00,0x7F,0xE0,0x19,0x80,0x30,0xC0,0x79,0xF0,0x4A,0x40,0x4A,0x40,0x39,0xC0,0x00,0x00}},
  {"运", {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x27,0xE0,0x00,0x00,0x40,0x00,0x2F,0xF0,0x02,0x00,0x62,0x40,0x24,0x60,0x2F,0xE0,0x20,0x00,0x70,0x00,0x4F,0xE0,0x00,0x00}},
  {"行", {0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x27,0xE0,0x40,0x00,0x10,0x00,0x27,0xE0,0x60,0x40,0xE0,0x40,0x20,0x40,0x20,0x40,0x20,0x40,0x20,0x40,0x21,0x80,0x00,0x00}},
  {"负", {0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x1F,0x00,0x11,0x00,0x22,0x00,0x7F,0xC0,0x20,0x40,0x22,0x40,0x24,0x40,0x24,0x40,0x04,0x00,0x19,0xC0,0x60,0x20,0x00,0x00}},
  {"载", {0x00,0x00,0x00,0x00,0x00,0x00,0x11,0x40,0x7F,0x20,0x11,0x00,0x7F,0xE0,0x11,0x00,0x7F,0x20,0x28,0xC0,0x7E,0xC0,0x08,0x80,0x1E,0x90,0x69,0x60,0x0A,0x60,0x00,0x00}},
  {"内", {0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x04,0x00,0x7F,0xE0,0x44,0x20,0x44,0x20,0x46,0x20,0x45,0x20,0x48,0xA0,0x70,0xE0,0x40,0x20,0x40,0x20,0x40,0xE0,0x00,0x00}},
  {"存", {0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x08,0x00,0x7F,0xE0,0x10,0x00,0x17,0xE0,0x20,0x40,0x61,0x80,0x2F,0xE0,0x21,0x00,0x21,0x00,0x21,0x00,0x23,0x00,0x00,0x00}},
  {"磁", {0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x20,0x7A,0x40,0x22,0x40,0x2F,0xF0,0x44,0x40,0x75,0xA0,0x5A,0xA0,0xD6,0xC0,0x52,0x40,0x56,0x60,0x75,0xA0,0x4F,0xD0,0x00,0x00}},
  {"盘", {0x00,0x00,0x00,0x00,0x04,0x00,0x1F,0xC0,0x24,0x40,0x22,0x40,0x7F,0xE0,0x24,0x40,0x62,0xC0,0x3F,0xC0,0x29,0x40,0x29,0x40,0x29,0x40,0x7F,0xE0,0x00,0x00,0x00,0x00}},
  {"核", {0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x80,0x20,0x80,0x27,0xE0,0x79,0x00,0x22,0x40,0x36,0x40,0x73,0xA0,0x6B,0x20,0xA4,0x40,0x20,0xC0,0x23,0x20,0x24,0x30,0x00,0x00}},
  {"菜", {0x10,0x80,0xFF,0xF0,0x10,0x80,0x00,0x00,0x03,0xF0,0xFF,0x10,0x04,0x30,0x46,0x20,0x60,0x40,0x06,0x00,0xFF,0xF0,0x0F,0x00,0x36,0x80,0x66,0x60,0xC6,0x10,0x06,0x00}},
  {"单", {0x20,0x40,0x30,0x40,0x10,0x80,0xFF,0xF0,0xC6,0x10,0xC6,0x10,0xFF,0xF0,0xC6,0x10,0xC6,0x10,0xFF,0xF0,0x06,0x00,0x06,0x00,0xFF,0xF0,0x06,0x00,0x06,0x00,0x06,0x00}},
  {"刷", {0x00,0x00,0xFE,0x00,0x82,0x80,0x82,0x80,0x82,0x80,0xFE,0x80,0x88,0x80,0xFE,0x80,0xCA,0x80,0xCA,0x80,0xCA,0x80,0x4A,0x80,0x4A,0x00,0x4E,0x00,0x08,0x00,0x08,0x70}},
  {"语", {0x8F,0xF0,0x41,0x00,0x61,0x00,0x0F,0xF0,0x02,0x10,0xC2,0x30,0x42,0x20,0x5F,0xF0,0x40,0x00,0x4F,0xF0,0x4C,0x10,0x5C,0x10,0x6C,0x10,0x4F,0xF0,0x0C,0x10,0x00,0x00}},
  {"音", {0x06,0x00,0x06,0x00,0xFF,0xF0,0x20,0x40,0x10,0xC0,0x10,0x80,0xFF,0xF0,0x00,0x00,0x7F,0xE0,0x40,0x20,0x40,0x20,0x7F,0xE0,0x40,0x20,0x40,0x20,0x7F,0xE0,0x40,0x20}},
};

const ZhGlyph *findZhGlyph(const char *utf8) {
  for (const ZhGlyph &glyph : kZhGlyphs) {
    if (strncmp(glyph.utf8, utf8, 3) == 0) return &glyph;
  }
  return nullptr;
}

const ZhGlyph kMenuGlyphs[] = {
  {"天", {0x00,0x00,0xFF,0xF0,0x04,0x00,0x04,0x00,0x04,0x00,0x04,0x00,0xFF,0xF0,0x06,0x00,0x06,0x00,0x0D,0x00,0x09,0x80,0x30,0xC0,0x60,0x60,0x80,0x10,0x00,0x00,0x00,0x00}},
  {"气", {0x00,0x00,0x30,0x00,0x20,0x00,0x3F,0xF0,0x40,0x00,0xDF,0xE0,0x80,0x00,0x7F,0xC0,0x00,0x40,0x00,0x40,0x00,0x40,0x00,0x40,0x00,0x50,0x00,0x30,0x00,0x30,0x00,0x00}},
  {"服", {0x00,0x00,0xFB,0xF0,0x8A,0x10,0x8A,0x10,0xFA,0x70,0x8A,0x00,0x8B,0xF0,0x8B,0x10,0xFA,0x90,0x8A,0xA0,0x8A,0x60,0x8A,0x40,0x8A,0xA0,0xB3,0x10,0x00,0x00,0x00,0x00}},
  {"务", {0x00,0x00,0x08,0x00,0x08,0x00,0x1F,0xE0,0x70,0x40,0x49,0x80,0x0F,0x00,0xF0,0xF0,0x04,0x00,0x7F,0xE0,0x0C,0x20,0x08,0x20,0x18,0x20,0x30,0x60,0xC1,0xC0,0x00,0x00}},
  {"器", {0x00,0x00,0x79,0xE0,0x49,0x20,0x49,0x20,0x79,0xE0,0x04,0x00,0xFF,0xF0,0x19,0x80,0x60,0x40,0xF9,0xF0,0x49,0x20,0x49,0x20,0x79,0xE0,0x49,0x20,0x00,0x00,0x00,0x00}},
  {"刷", {0x00,0x00,0x00,0x10,0xFE,0x10,0x82,0x90,0x82,0x90,0xFE,0x90,0x88,0x90,0xBE,0x90,0xAA,0x90,0xAA,0x90,0xAA,0x90,0xAA,0x10,0xAE,0x10,0x88,0x10,0x08,0x70,0x00,0x00}},
  {"新", {0x00,0x00,0x20,0x10,0x20,0x60,0xFD,0x80,0x49,0x00,0x49,0x00,0xFD,0xF0,0x21,0x20,0x21,0x20,0xFD,0x20,0x31,0x20,0x6A,0x20,0xA2,0x20,0xA6,0x20,0x24,0x20,0x00,0x00}},
  {"语", {0x00,0x00,0xCF,0xF0,0x21,0x00,0x0F,0xE0,0x02,0x20,0xE2,0x20,0x22,0x20,0x3F,0xF0,0x20,0x00,0x27,0xE0,0x24,0x20,0x34,0x20,0x47,0xE0,0x04,0x20,0x00,0x00,0x00,0x00}},
  {"音", {0x06,0x00,0x7F,0xF0,0x10,0xC0,0x10,0x80,0x10,0x80,0xFF,0xF0,0x00,0x00,0x3F,0xC0,0x20,0x40,0x20,0x40,0x3F,0xC0,0x20,0x40,0x3F,0xC0,0x20,0x40,0x00,0x00,0x00,0x00}},
};

const ZhGlyph *findMenuGlyph(const char *utf8) {
  for (const ZhGlyph &glyph : kMenuGlyphs) {
    if (strncmp(glyph.utf8, utf8, 3) == 0) return &glyph;
  }
  return nullptr;
}

void drawZhText(uint8_t x, uint8_t y, const char *text, uint16_t color) {
  const char *cursor = text;
  while (*cursor && x < kTftWidth - 5) {
    if ((static_cast<uint8_t>(*cursor) & 0x80) == 0) {
      char ascii[2] = {*cursor++, '\0'};
      drawText(x, y + 4, ascii, color);
      x += 6;
      continue;
    }
    const ZhGlyph *glyph = findZhGlyph(cursor);
    if (glyph) {
      for (uint8_t row = 0; row < 16; ++row) {
        const uint8_t sourceRow = 15 - row;
        const uint16_t pixels = (static_cast<uint16_t>(glyph->bits[sourceRow * 2]) << 8) | glyph->bits[sourceRow * 2 + 1];
        for (uint8_t column = 0; column < 12; ++column) {
          if (pixels & (1U << (15 - column))) tftFillRect(x + column, y + row, 1, 1, color);
        }
      }
    }
    x += 12;
    cursor += 3;
  }
}

void drawMenuZhText(uint8_t x, uint8_t y, const char *text, uint16_t color) {
  const char *cursor = text;
  while (*cursor && x < kTftWidth - 5) {
    if ((static_cast<uint8_t>(*cursor) & 0x80) == 0) {
      char ascii[2] = {*cursor++, '\0'};
      drawText(x, y + 4, ascii, color);
      x += 6;
      continue;
    }
    const ZhGlyph *glyph = findMenuGlyph(cursor);
    if (glyph) {
      for (uint8_t row = 0; row < 16; ++row) {
        const uint8_t sourceRow = 15 - row;
        const uint16_t pixels = (static_cast<uint16_t>(glyph->bits[sourceRow * 2]) << 8) | glyph->bits[sourceRow * 2 + 1];
        for (uint8_t column = 0; column < 12; ++column) {
          if (pixels & (1U << (15 - column))) tftFillRect(x + column, y + row, 1, 1, color);
        }
      }
    }
    x += 14;
    cursor += 3;
  }
}

void drawVoiceReplyBitmap(uint8_t y, uint16_t color) {
  constexpr uint8_t bytesPerRow = kVoiceReplyBitmapWidth / 8;
  for (uint8_t row = 0; row < kVoiceReplyBitmapHeight; ++row) {
    for (uint8_t columnByte = 0; columnByte < bytesPerRow; ++columnByte) {
      const uint8_t sourceRow = kVoiceReplyBitmapHeight - 1 - row;
      const uint8_t pixels = voiceReplyBitmap[sourceRow * bytesPerRow + columnByte];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        if (pixels & (1U << (7 - bit))) {
          tftFillRect(columnByte * 8 + bit, y + row, 1, 1, color);
        }
      }
    }
  }
}

void drawFilledCircle(int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
  const int16_t rr = radius * radius;
  for (int16_t dy = -radius; dy <= radius; ++dy) {
    int16_t dx = radius;
    while (dx > 0 && dx * dx + dy * dy > rr) --dx;
    const int16_t x0 = cx - dx;
    const int16_t y = cy + dy;
    const int16_t width = dx * 2 + 1;
    if (x0 >= 0 && y >= 0 && x0 + width <= kTftWidth && y < kTftHeight) {
      tftFillRect(static_cast<uint8_t>(x0), static_cast<uint8_t>(y), static_cast<uint8_t>(width), 1, color);
    }
  }
}

void drawLineSafe(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  const int16_t dx = abs(x1 - x0);
  const int16_t sx = x0 < x1 ? 1 : -1;
  const int16_t dy = -abs(y1 - y0);
  const int16_t sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  while (true) {
    if (x0 >= 0 && x0 < kTftWidth && y0 >= 0 && y0 < kTftHeight) {
      tftFillRect(static_cast<uint8_t>(x0), static_cast<uint8_t>(y0), 1, 1, color);
    }
    if (x0 == x1 && y0 == y1) break;
    const int16_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void drawWatchRing() {
  drawFilledCircle(64, 57, 42, 0x0841);
  drawFilledCircle(64, 57, 39, 0x0000);
  drawFilledCircle(64, 57, 34, 0x1082);
  const int8_t marks[][2] = {
      {64, 17}, {84, 23}, {99, 38}, {105, 57}, {99, 76}, {84, 91},
      {64, 97}, {44, 91}, {29, 76}, {23, 57}, {29, 38}, {44, 23},
  };
  for (uint8_t index = 0; index < 12; ++index) {
    const uint16_t color = (index % 3 == 0) ? 0x07E0 : 0x39E7;
    const uint8_t size = (index % 3 == 0) ? 3 : 2;
    tftFillRect(marks[index][0] - size / 2, marks[index][1] - size / 2, size, size, color);
  }
}

void drawArc(int16_t cx, int16_t cy, int16_t radius, int16_t startDeg, int16_t endDeg, uint16_t color, uint8_t thickness) {
  for (int16_t degree = startDeg; degree <= endDeg; degree += 4) {
    const float radians = degree * PI / 180.0f;
    const int16_t x = cx + static_cast<int16_t>(cosf(radians) * radius);
    const int16_t y = cy + static_cast<int16_t>(sinf(radians) * radius);
    drawFilledCircle(x, y, thickness, color);
  }
}

void drawSportFaceTicks() {
  for (int16_t degree = 0; degree < 360; degree += 15) {
    const float radians = degree * PI / 180.0f;
    const int16_t x = 64 + static_cast<int16_t>(cosf(radians) * 58);
    const int16_t y = 64 + static_cast<int16_t>(sinf(radians) * 58);
    const uint16_t color = degree % 45 == 0 ? 0x07FF : 0x03EF;
    tftFillRect(constrain(x, 1, 126), constrain(y, 1, 126), 2, 2, color);
  }
}

void drawPanel(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint16_t fill, uint16_t border) {
  tftFillRect(x, y, width, height, fill);
  tftFillRect(x, y, width, 1, border);
  tftFillRect(x, y + height - 1, width, 1, border);
  tftFillRect(x, y, 1, height, border);
  tftFillRect(x + width - 1, y, 1, height, border);
}

bool watchFaceVisible() {
  return uiPageIs(UiPage::Watch) && !voiceResultHeld();
}

void drawDashboardHeader(const char *title, uint16_t accent) {
  tftFillRect(0, 0, kTftWidth, 20, 0x0841);
  tftFillRect(0, 0, 4, 20, accent);
  tftFillRect(4, 19, 124, 1, 0x2104);
  drawZhText(9, 2, title, 0xFFFF);
  tftFillRect(112, 6, 3, 8, 0x39E7);
  tftFillRect(117, 3, 3, 11, 0x9CF3);
  tftFillRect(122, 1, 3, 13, accent);
}

void drawMeter(uint8_t x, uint8_t y, uint8_t width, float percent, uint16_t color) {
  percent = constrain(percent, 0.0f, 100.0f);
  tftFillRect(x, y, width, 6, 0x18C3);
  const uint8_t fill = static_cast<uint8_t>((width * percent) / 100.0f);
  if (fill > 0) tftFillRect(x, y, fill, 6, color);
}

VoiceRenderMode currentVoiceRenderMode() {
  if (voiceStopRequested || voiceStatus == "UP") return VoiceRenderMode::Uploading;
  if (voiceRecording) return VoiceRenderMode::Recording;
  if (voiceStatus == "SPEAK") return VoiceRenderMode::Speaking;
  if (voiceStatus == "OK") return VoiceRenderMode::Result;
  return VoiceRenderMode::Idle;
}

void renderVoiceFrame() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("服务", 0x07E0);
  drawText(42, 6, "AI", 0xFFFF, 1);
  drawText(60, 6, voiceStatus, voiceStatus == "ERR" ? 0xF800 : 0xFFFF, 1);
  drawText(4, 119, "P4 EXIT P5- V" + String(speakerVolumePercent) + " P6+", 0x9CF3, 1);
}

void clearVoiceContentArea() {
  tftFillRect(0, 22, kTftWidth, 95, 0x0000);
}

void renderVoiceUploadingContent() {
  clearVoiceContentArea();
  drawPanel(31, 32, 66, 55, 0x0841, 0x2945);
  tftFillRect(62, 42, 4, 4, 0x07FF);
  tftFillRect(58, 46, 12, 4, 0x07FF);
  tftFillRect(54, 50, 20, 4, 0x07FF);
  tftFillRect(50, 54, 28, 5, 0x07FF);
  tftFillRect(59, 59, 10, 20, 0x07FF);
  drawText(56, 94, "UP", 0x07FF, 1);
  drawText(40, 110, "AI WAIT", 0x9CF3, 1);
}

void renderVoiceRecordingContent(bool full) {
  const unsigned long elapsedSecond = (millis() - voiceRecordStartedMs) / 1000UL;
  if (!full && elapsedSecond == lastVoiceRenderedSecond) return;
  clearVoiceContentArea();
  const unsigned long limit = (voicePcmCapacity * 1000UL) / (kVoiceSampleRate * 2UL);
  drawPanel(30, 31, 68, 56, 0x0841, 0x2945);
  tftFillRect(50, 43, 28, 28, 0xF800);
  tftFillRect(56, 49, 16, 16, 0x0000);
  drawText(24, 94, "REC", 0xF800, 2);
  drawText(64, 98, String(elapsedSecond) + "/" + String(limit / 1000UL) + "S", 0xFFFF, 1);
  drawText(31, 114, voiceCallMode ? "AUTO STOP" : "PIN7 STOP", 0x9CF3, 1);
  lastVoiceRenderedSecond = elapsedSecond;
}

void renderVoiceSpeakingContent() {
  clearVoiceContentArea();
  if (voiceReplyBitmapValid) {
    drawVoiceReplyBitmap(24, 0xFFFF);
    for (uint8_t column = 0; column < 7; ++column) {
      const uint8_t height = 4 + ((column * 5) % 10);
      tftFillRect(31 + column * 10, 115 - height / 2, 5, height, 0x07E0);
    }
  } else {
    drawText(24, 60, "SPEAK", 0x07E0, 2);
  }
}

void renderVoiceResultContent() {
  clearVoiceContentArea();
  if (voiceReplyBitmapValid) {
    drawVoiceReplyBitmap(24, 0xFFFF);
  } else {
    drawText(4, 50, "TEXT", 0xFFFF, 1);
    drawText(4, 68, String(voiceTranscript.length()) + "B", 0xFFFF, 2);
  }
}

void renderVoiceIdleContent() {
  clearVoiceContentArea();
  drawPanel(32, 34, 64, 50, 0x0841, 0x2945);
  tftFillRect(54, 45, 20, 24, 0x07E0);
  tftFillRect(50, 69, 28, 5, 0x07E0);
  drawText(35, 94, voiceCallMode ? "CALL ON" : "P7 CALL", 0xFFE0, 1);
  drawText(36, 111, voiceCallMode ? "LISTEN" : "AUTO TALK", 0x9CF3, 1);
  if (voiceStatus == "SHORT") drawText(20, 84, "REC LONGER", 0xF800, 1);
  if (voiceStatus == "NOSOUND") drawText(30, 84, "NO SOUND", 0xF800, 1);
  if (voiceStatus == "NOSPEECH") drawText(25, 84, "NO SPEECH", 0xF800, 1);
  if (voiceStatus == "NOWIFI") drawText(32, 84, "NO WIFI", 0xF800, 1);
  if (voiceStatus == "MEM") drawText(38, 84, "NO MEM", 0xF800, 1);
}

void renderVoicePage() {
  const VoiceRenderMode mode = currentVoiceRenderMode();
  const bool full = mode != lastVoiceRenderMode ||
      lastVoiceRenderedStatus != voiceStatus ||
      lastVoiceRenderedVolume != speakerVolumePercent;
  if (full) renderVoiceFrame();

  if (mode == VoiceRenderMode::Uploading) renderVoiceUploadingContent();
  else if (mode == VoiceRenderMode::Recording) renderVoiceRecordingContent(full);
  else if (mode == VoiceRenderMode::Speaking) renderVoiceSpeakingContent();
  else if (mode == VoiceRenderMode::Result) renderVoiceResultContent();
  else renderVoiceIdleContent();

  lastVoiceRenderMode = mode;
  lastVoiceRenderedStatus = voiceStatus;
  lastVoiceRenderedVolume = speakerVolumePercent;
}

void renderServerOffline() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("服务器", 0xF800);
  drawPanel(8, 42, 112, 36, 0x0841, 0xF800);
  drawZhText(18, 48, "连接失败", 0xF800);
  drawText(8, 119, "P4 BACK", 0x9CF3, 1);
}

void renderServerFrame() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("服务器", 0x07E0);
  drawPanel(4, 24, 120, 22, 0x0841, 0x2945);
  drawZhText(10, 27, "运行", 0xFFE0);
  drawPanel(4, 51, 120, 18, 0x0841, 0x2945);
  drawZhText(10, 52, "负载", 0x07FF);
  drawPanel(4, 74, 120, 18, 0x0841, 0x2945);
  drawZhText(10, 75, "内存", 0xF81F);
  drawPanel(4, 97, 120, 18, 0x0841, 0x2945);
  drawZhText(10, 98, "磁盘", 0xFFE0);
}

void renderServerDynamicOnly() {
  const float cpuPercent = serverStatus.cpuCount > 0 ? serverStatus.load1m * 100.0f / serverStatus.cpuCount : 0;
  tftFillRect(42, 29, 78, 14, 0x0841);
  drawText(42, 31, uptimeText(serverStatus.uptimeSeconds), 0xFFFF, 1);
  tftFillRect(43, 56, 76, 10, 0x0841);
  drawText(43, 57, displayNumber(serverStatus.load1m), 0xFFFF, 1);
  drawMeter(74, 57, 43, cpuPercent, 0x07FF);
  tftFillRect(43, 79, 76, 10, 0x0841);
  drawText(43, 80, displayNumber(serverStatus.memoryUsedPercent) + "%", 0xFFFF, 1);
  drawMeter(74, 80, 43, serverStatus.memoryUsedPercent, 0xF81F);
  tftFillRect(43, 102, 76, 10, 0x0841);
  drawText(43, 103, displayNumber(serverStatus.diskUsedPercent) + "%", 0xFFFF, 1);
  drawMeter(74, 103, 43, serverStatus.diskUsedPercent, 0xFFE0);
  tftFillRect(4, 119, 52, 8, 0x0000);
  drawText(4, 119, serverStatus.sampledAt.length() >= 16 ? serverStatus.sampledAt.substring(11, 16) : "--:--", 0x9CF3, 1);
}

void renderServerStatus() {
  if (!serverStatus.valid) {
    if (!serverPageRendered || lastServerRenderedValid) renderServerOffline();
    serverPageRendered = true;
    lastServerRenderedValid = false;
    return;
  }
  if (!serverPageRendered || !lastServerRenderedValid) renderServerFrame();
  renderServerDynamicOnly();
  serverPageRendered = true;
  lastServerRenderedValid = true;
}

void drawMenuRow(uint8_t y, const MenuEntry &entry, bool selected) {
  drawPanel(6, y, 116, 18, selected ? 0x2104 : 0x0841, selected ? entry.accent : 0x2945);
  if (selected) {
    tftFillRect(10, y + 5, 5, 8, entry.accent);
    drawText(18, y + 5, ">", entry.accent, 1);
  } else {
    tftFillRect(11, y + 7, 4, 4, entry.accent);
  }
  drawMenuZhText(27, y + 1, entry.label, selected ? 0xFFFF : 0xD6BA);
}

void drawMenuTile(uint8_t x, uint8_t y, const char *pin, const char *label, uint16_t accent, uint8_t icon) {
  drawPanel(x, y, 55, 44, 0x0841, 0x2945);
  if (icon == 0) {
    drawFilledCircle(x + 18, y + 16, 7, accent);
    tftFillRect(x + 17, y + 4, 2, 5, accent);
    tftFillRect(x + 17, y + 23, 2, 5, accent);
    tftFillRect(x + 6, y + 15, 5, 2, accent);
    tftFillRect(x + 25, y + 15, 5, 2, accent);
  } else if (icon == 1) {
    tftFillRect(x + 10, y + 10, 20, 4, accent);
    tftFillRect(x + 10, y + 17, 20, 4, accent);
    tftFillRect(x + 10, y + 24, 20, 4, accent);
    tftFillRect(x + 13, y + 11, 3, 2, 0x0000);
    tftFillRect(x + 13, y + 18, 3, 2, 0x0000);
    tftFillRect(x + 13, y + 25, 3, 2, 0x0000);
  } else if (icon == 2) {
    tftFillRect(x + 11, y + 14, 19, 4, accent);
    tftFillRect(x + 25, y + 10, 4, 12, accent);
    tftFillRect(x + 20, y + 8, 8, 4, accent);
    tftFillRect(x + 10, y + 25, 19, 4, accent);
    tftFillRect(x + 10, y + 21, 4, 12, accent);
    tftFillRect(x + 11, y + 31, 8, 4, accent);
  } else {
    tftFillRect(x + 16, y + 9, 10, 17, accent);
    tftFillRect(x + 13, y + 22, 16, 4, accent);
    tftFillRect(x + 20, y + 26, 3, 7, accent);
    tftFillRect(x + 15, y + 33, 13, 3, accent);
  }
  drawText(x + 34, y + 8, pin, 0x9CF3, 1);
  drawMenuZhText(x + 12, y + 27, label, 0xFFFF);
}

uint8_t menuWindowStart() {
  return menuSelectedIndex < 4 ? 0 : menuSelectedIndex - 3;
}

void renderMenuFrame() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("菜单", 0xFFE0);
  drawText(4, 112, "P4 BACK P5 UP P6 DN", 0x9CF3, 1);
  drawText(42, 121, "P7 OK", 0xFFE0, 1);
}

void renderMenuList() {
  const uint8_t start = menuWindowStart();
  tftFillRect(4, 22, 120, 86, 0x0000);
  for (uint8_t row = 0; row < 4; ++row) {
    const uint8_t itemIndex = start + row;
    if (itemIndex >= kMenuEntryCount) break;
    drawMenuRow(24 + row * 21, kMenuEntries[itemIndex], itemIndex == menuSelectedIndex);
  }
  lastMenuRenderedStart = start;
  lastMenuRenderedIndex = menuSelectedIndex;
}

void renderMenuDynamicOnly() {
  if (menuWindowStart() != lastMenuRenderedStart || lastMenuRenderedIndex >= kMenuEntryCount) {
    renderMenuList();
    return;
  }
  const uint8_t previousRow = lastMenuRenderedIndex - lastMenuRenderedStart;
  const uint8_t currentRow = menuSelectedIndex - lastMenuRenderedStart;
  if (previousRow < 4) {
    drawMenuRow(24 + previousRow * 21, kMenuEntries[lastMenuRenderedIndex], false);
  }
  if (currentRow < 4) {
    drawMenuRow(24 + currentRow * 21, kMenuEntries[menuSelectedIndex], true);
  }
  lastMenuRenderedIndex = menuSelectedIndex;
}

void renderMenuPage() {
  const bool full = lastMenuRenderedIndex >= kMenuEntryCount || lastMenuRenderedStart == 0xFF;
  if (full) renderMenuFrame();
  if (full) renderMenuList();
  else renderMenuDynamicOnly();
}

void renderSettingsFrame() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("SET", 0xFFE0);
  drawPanel(6, 25, 116, 22, 0x0841, 0x2945);
  drawText(12, 32, "VOL", 0xFFE0, 1);
  drawPanel(6, 61, 116, 22, 0x0841, 0x2945);
  drawText(12, 68, "OTA", 0x07FF, 1);
  drawPanel(6, 90, 116, 22, 0x0841, 0x2945);
  drawText(72, 97, "V" FIRMWARE_VERSION, 0x9CF3, 1);
  drawText(6, 119, "P4 BACK P5- P6+ P7 WIFI", 0x9CF3, 1);
}

void renderSettingsDynamicOnly(bool force) {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (force || lastSettingsRenderedVolume != speakerVolumePercent) {
    tftFillRect(42, 28, 76, 28, 0x0841);
    drawText(43, 30, String(speakerVolumePercent) + "%", 0xFFFF, 2);
    drawMeter(12, 51, 104,
        (speakerVolumePercent - kSpeakerVolumeMinPercent) * 100.0f /
            (kSpeakerVolumeMaxPercent - kSpeakerVolumeMinPercent),
        0xFFE0);
    lastSettingsRenderedVolume = speakerVolumePercent;
  }
  if (force || lastSettingsRenderedOtaStatus != otaStatus) {
    tftFillRect(42, 67, 76, 10, 0x0841);
    const uint16_t color = otaStatus.indexOf("ERR") >= 0 || otaStatus.indexOf("HTTP") >= 0 ||
        otaStatus.indexOf("FAIL") >= 0 ? 0xF800 : 0xFFFF;
    drawText(43, 68, otaStatus.substring(0, 12), color, 1);
    lastSettingsRenderedOtaStatus = otaStatus;
  }
  if (force || lastSettingsRenderedWifiConnected != wifiConnected) {
    tftFillRect(11, 96, 58, 10, 0x0841);
    drawText(12, 97, wifiConnected ? "WIFI ON" : "WIFI OFF", 0xFFFF, 1);
    lastSettingsRenderedWifiConnected = wifiConnected;
  }
}

void renderSettingsPage() {
  if (!settingsPageRendered) {
    renderSettingsFrame();
    settingsPageRendered = true;
    renderSettingsDynamicOnly(true);
    return;
  }
  renderSettingsDynamicOnly(false);
}

String wifiSetupStatusText() {
  if (WiFi.status() == WL_CONNECTED) {
    return String("STA ") + WiFi.localIP().toString();
  }
  return "STA OFF";
}

void renderWifiSetupFallback() {
  if (wifiSetupPageRendered) return;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("WIFI", 0x07FF);
  drawText(8, 32, "AP: ESP32S3-Setup", 0xFFFF, 1);
  drawText(8, 48, "OPEN:", 0x07FF, 1);
  drawText(8, 64, kPortalUrl, 0xFFE0, 1);
  drawText(8, 103, "P4 BACK", 0x9CF3, 1);
  wifiSetupPageRendered = true;
}

void updateWifiSetupLvglLabels() {
  lvglSetLabel(wifiSetupLabelStatus, wifiSetupStatusText());
  lvglSetLabel(wifiSetupLabelAp, "AP ESP32S3-Setup");
  lvglSetLabel(wifiSetupLabelHint, "P4 BACK  192.168.4.1");
}

void buildWifiSetupLvglPage() {
  lvglWatchFaceBuilt = false;
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070b), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *title = createWatchLabel(screen, &lv_font_montserrat_12, lv_color_hex(0x19d3ff));
  lv_label_set_text(title, "WIFI SETUP");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t *qr = lv_qrcode_create(screen, 80, lv_color_hex(0x05070b), lv_color_hex(0xffffff));
  lv_qrcode_update(qr, kPortalUrl, strlen(kPortalUrl));
  lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 20);

  wifiSetupLabelStatus = createWatchLabel(screen, &lv_font_montserrat_8, lv_color_hex(0x7dff7a));
  lv_obj_align(wifiSetupLabelStatus, LV_ALIGN_BOTTOM_MID, 0, -22);

  wifiSetupLabelAp = createWatchLabel(screen, &lv_font_montserrat_8, lv_color_hex(0xd6dbe6));
  lv_obj_align(wifiSetupLabelAp, LV_ALIGN_BOTTOM_MID, 0, -13);

  wifiSetupLabelHint = createWatchLabel(screen, &lv_font_montserrat_8, lv_color_hex(0x8e9bb1));
  lv_obj_align(wifiSetupLabelHint, LV_ALIGN_BOTTOM_MID, 0, -3);

  wifiSetupPageRendered = true;
}

void renderWifiSetupPage() {
  startConfigurationPortal(true);
  if (!lvglReady) {
    renderWifiSetupFallback();
    return;
  }
  if (!wifiSetupPageRendered) buildWifiSetupLvglPage();
  updateWifiSetupLvglLabels();
  lv_refr_now(nullptr);
}

void renderMpuPage() {
  lastDynamicSensorPage = SensorPageId::Mpu;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("MPU", 0x07FF);
  if (!mpuReady || !mpuData.valid) {
    drawPanel(8, 36, 112, 44, 0x0841, 0xF800);
    drawText(20, 48, "MPU6050 OFF", 0xF800, 1);
    drawText(15, 64, "CHECK SDA/SCL", 0x9CF3, 1);
    drawText(6, 119, "P4 BACK", 0x9CF3, 1);
    return;
  }

  drawPanel(6, 24, 116, 44, 0x0841, 0x2945);
  drawText(12, 29, "ACC g", 0x07FF, 1);
  drawText(12, 42, "X", 0x9CF3, 1);
  drawText(24, 42, displayNumber(mpuData.ax), 0xFFFF, 1);
  drawText(63, 42, "Y", 0x9CF3, 1);
  drawText(75, 42, displayNumber(mpuData.ay), 0xFFFF, 1);
  drawText(12, 55, "Z", 0x9CF3, 1);
  drawText(24, 55, displayNumber(mpuData.az), 0xFFFF, 1);

  drawPanel(6, 72, 116, 38, 0x0841, 0x2945);
  drawText(12, 77, "GYRO d/s", 0xFFE0, 1);
  drawText(12, 90, "X", 0x9CF3, 1);
  drawText(24, 90, displayNumber(mpuData.gx), 0xFFFF, 1);
  drawText(63, 90, "Y", 0x9CF3, 1);
  drawText(75, 90, displayNumber(mpuData.gy), 0xFFFF, 1);
  drawText(12, 101, "Z", 0x9CF3, 1);
  drawText(24, 101, displayNumber(mpuData.gz), 0xFFFF, 1);

  drawText(78, 58, String(displayNumber(mpuData.temperature)) + "C", 0xF81F, 1);
  drawText(58, 117, mpuGestureStatus, mpuGestureStatus == "READY" ? 0x9CF3 : 0x07E0, 1);
  drawText(6, 117, "P4 BACK P7 CAL", 0x9CF3, 1);
}

void renderLevelPage() {
  lastDynamicSensorPage = SensorPageId::Level;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("LEVEL", 0xFFE0);
  if (!mpuReady || !mpuData.valid) {
    drawPanel(8, 42, 112, 36, 0x0841, 0xF800);
    drawText(20, 54, "MPU6050 OFF", 0xF800, 1);
    drawText(6, 117, "P4 BACK P7 CAL", 0x9CF3, 1);
    return;
  }

  const float zx = constrain(mpuAxZeroed(), -1.0f, 1.0f);
  const float zy = constrain(mpuAyZeroed(), -1.0f, 1.0f);
  const int16_t centerX = 64;
  const int16_t centerY = 65;
  const int16_t bubbleX = centerX + static_cast<int16_t>(zx * 38.0f);
  const int16_t bubbleY = centerY + static_cast<int16_t>(zy * 38.0f);
  drawFilledCircle(centerX, centerY, 45, 0x0841);
  drawFilledCircle(centerX, centerY, 42, 0x0000);
  drawFilledCircle(centerX, centerY, 24, 0x1082);
  drawLineSafe(centerX - 43, centerY, centerX + 43, centerY, 0x2945);
  drawLineSafe(centerX, centerY - 43, centerX, centerY + 43, 0x2945);
  drawFilledCircle(centerX, centerY, 4, 0x39E7);
  drawFilledCircle(bubbleX, bubbleY, 8, 0xFFE0);
  drawFilledCircle(bubbleX, bubbleY, 4, 0xFFFF);
  drawText(4, 22, "X" + displayNumber(zx), 0x07FF, 1);
  drawText(82, 22, "Y" + displayNumber(zy), 0xF81F, 1);
  drawText(6, 117, "P4 BACK P7 CAL", mpuCalibration.valid ? 0x9CF3 : 0xFFE0, 1);
}

void renderAnglePage() {
  lastDynamicSensorPage = SensorPageId::Angle;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("ANGLE", 0x07E0);
  if (!mpuReady || !mpuData.valid) {
    drawPanel(8, 42, 112, 36, 0x0841, 0xF800);
    drawText(20, 54, "MPU6050 OFF", 0xF800, 1);
    drawText(6, 117, "P4 BACK P7 CAL", 0x9CF3, 1);
    return;
  }

  drawPanel(6, 25, 116, 24, 0x0841, 0x2945);
  drawText(12, 31, "ROLL", 0x07FF, 1);
  drawText(58, 28, displayNumber(mpuRollDeg), 0xFFFF, 2);
  drawPanel(6, 54, 116, 24, 0x0841, 0x2945);
  drawText(12, 60, "PITCH", 0xFFE0, 1);
  drawText(58, 57, displayNumber(mpuPitchDeg), 0xFFFF, 2);
  drawPanel(6, 83, 116, 24, 0x0841, 0x2945);
  drawText(12, 89, "YAW/s", 0xF81F, 1);
  drawText(58, 86, displayNumber(mpuYawRateDeg), 0xFFFF, 2);
  drawText(6, 117, "P4 BACK P7 CAL", mpuCalibration.valid ? 0x9CF3 : 0xFFE0, 1);
}

void renderOdometerPage() {
  lastDynamicSensorPage = SensorPageId::Odometer;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("ODO", 0xF81F);
  const float meters = odometerSteps * kOdometerStepMeters;
  drawPanel(8, 25, 112, 30, 0x0841, 0x2945);
  drawText(16, 31, "STEPS", 0x07FF, 1);
  drawText(64, 28, String(odometerSteps), 0xFFFF, 2);
  drawPanel(8, 61, 112, 30, 0x0841, 0x2945);
  drawText(16, 67, "DIST", 0xFFE0, 1);
  drawText(58, 64, String(meters, meters < 100.0f ? 1 : 0) + "m", 0xFFFF, 2);
  drawPanel(8, 97, 112, 16, 0x0841, 0x2945);
  drawText(14, 102, "MOVE", 0x9CF3, 1);
  drawText(52, 102, isfinite(lastOdometerAccel) ? displayNumber(lastOdometerAccel) : "--", 0xFFFF, 1);
  drawText(6, 117, "P4 BACK P5 RST P7 CAL", 0x9CF3, 1);
}

void renderMotionPage() {
  lastDynamicSensorPage = SensorPageId::Motion;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("EVENT", 0xFD20);
  if (!mpuReady || !mpuData.valid) {
    drawPanel(8, 42, 112, 36, 0x0841, 0xF800);
    drawText(20, 54, "MPU6050 OFF", 0xF800, 1);
    drawText(6, 117, "P4 BACK P7 CAL", 0x9CF3, 1);
    return;
  }

  drawPanel(6, 24, 116, 21, 0x0841, 0x2945);
  drawText(12, 30, "STATE", 0xFFE0, 1);
  drawText(60, 30, mpuGestureStatus, mpuGestureStatus == "STILL" ? 0x07E0 : 0xFFFF, 1);
  drawPanel(6, 50, 116, 20, 0x0841, 0x2945);
  drawText(12, 55, "A", 0x07FF, 1);
  drawText(25, 55, displayNumber(mpuAccelMagnitude), 0xFFFF, 1);
  drawText(65, 55, "G", 0xF81F, 1);
  drawText(78, 55, displayNumber(mpuGyroMagnitude), 0xFFFF, 1);
  drawPanel(6, 75, 116, 20, 0x0841, 0x2945);
  drawText(12, 80, "SHA", 0x07FF, 1);
  drawText(38, 80, String(shakeCount), 0xFFFF, 1);
  drawText(66, 80, "UP", 0x07E0, 1);
  drawText(88, 80, String(raiseCount), 0xFFFF, 1);
  drawPanel(6, 100, 116, 15, 0x0841, 0x2945);
  drawText(12, 103, "FALL?", 0xF800, 1);
  drawText(50, 103, String(fallCount), 0xFFFF, 1);
  drawText(77, 103, mpuLedFeedback ? "LED ON" : "LED OFF", mpuLedFeedback ? 0x07E0 : 0x9CF3, 1);
  drawText(6, 117, "P4 BACK P5 CLR P6 LED P7 CAL", 0x9CF3, 1);
}

SensorPageId currentSensorPage() {
  if (uiPageIs(UiPage::MpuData)) return SensorPageId::Mpu;
  if (uiPageIs(UiPage::MpuLevel)) return SensorPageId::Level;
  if (uiPageIs(UiPage::MpuAngle)) return SensorPageId::Angle;
  if (uiPageIs(UiPage::MpuOdometer)) return SensorPageId::Odometer;
  if (uiPageIs(UiPage::MpuMotion)) return SensorPageId::Motion;
  return SensorPageId::None;
}

void drawMpuOfflineDynamic() {
  tftFillRect(8, 36, 112, 50, 0x0000);
  drawPanel(8, 36, 112, 44, 0x0841, 0xF800);
  drawText(20, 48, "MPU6050 OFF", 0xF800, 1);
  drawText(15, 64, "CHECK SDA/SCL", 0x9CF3, 1);
}

void renderMpuDynamicOnly() {
  if (!mpuReady || !mpuData.valid) {
    drawMpuOfflineDynamic();
    return;
  }
  tftFillRect(22, 40, 95, 26, 0x0841);
  drawText(63, 42, "Y", 0x9CF3, 1);
  drawText(24, 42, displayNumber(mpuData.ax), 0xFFFF, 1);
  drawText(75, 42, displayNumber(mpuData.ay), 0xFFFF, 1);
  drawText(24, 55, displayNumber(mpuData.az), 0xFFFF, 1);
  drawText(78, 58, String(displayNumber(mpuData.temperature)) + "C", 0xF81F, 1);

  tftFillRect(22, 88, 95, 22, 0x0841);
  drawText(63, 90, "Y", 0x9CF3, 1);
  drawText(24, 90, displayNumber(mpuData.gx), 0xFFFF, 1);
  drawText(75, 90, displayNumber(mpuData.gy), 0xFFFF, 1);
  drawText(24, 101, displayNumber(mpuData.gz), 0xFFFF, 1);

  tftFillRect(58, 117, 42, 8, 0x0000);
  drawText(58, 117, mpuGestureStatus, mpuGestureStatus == "READY" ? 0x9CF3 : 0x07E0, 1);
}

void renderLevelDynamicOnly() {
  if (!mpuReady || !mpuData.valid) {
    drawMpuOfflineDynamic();
    return;
  }
  const float zx = constrain(mpuAxZeroed(), -1.0f, 1.0f);
  const float zy = constrain(mpuAyZeroed(), -1.0f, 1.0f);
  const int16_t centerX = 64;
  const int16_t centerY = 65;
  const int16_t bubbleX = centerX + static_cast<int16_t>(zx * 38.0f);
  const int16_t bubbleY = centerY + static_cast<int16_t>(zy * 38.0f);
  tftFillRect(0, 21, 128, 92, 0x0000);
  drawFilledCircle(centerX, centerY, 45, 0x0841);
  drawFilledCircle(centerX, centerY, 42, 0x0000);
  drawFilledCircle(centerX, centerY, 24, 0x1082);
  drawLineSafe(centerX - 43, centerY, centerX + 43, centerY, 0x2945);
  drawLineSafe(centerX, centerY - 43, centerX, centerY + 43, 0x2945);
  drawFilledCircle(centerX, centerY, 4, 0x39E7);
  drawFilledCircle(bubbleX, bubbleY, 8, 0xFFE0);
  drawFilledCircle(bubbleX, bubbleY, 4, 0xFFFF);
  drawText(4, 22, "X" + displayNumber(zx), 0x07FF, 1);
  drawText(82, 22, "Y" + displayNumber(zy), 0xF81F, 1);
}

void renderAngleDynamicOnly() {
  if (!mpuReady || !mpuData.valid) {
    drawMpuOfflineDynamic();
    return;
  }
  tftFillRect(56, 27, 60, 80, 0x0841);
  drawText(58, 28, displayNumber(mpuRollDeg), 0xFFFF, 2);
  drawText(58, 57, displayNumber(mpuPitchDeg), 0xFFFF, 2);
  drawText(58, 86, displayNumber(mpuYawRateDeg), 0xFFFF, 2);
}

void renderOdometerDynamicOnly() {
  const float meters = odometerSteps * kOdometerStepMeters;
  tftFillRect(62, 27, 52, 65, 0x0841);
  drawText(64, 28, String(odometerSteps), 0xFFFF, 2);
  drawText(58, 64, String(meters, meters < 100.0f ? 1 : 0) + "m", 0xFFFF, 2);
  tftFillRect(50, 101, 42, 10, 0x0841);
  drawText(52, 102, isfinite(lastOdometerAccel) ? displayNumber(lastOdometerAccel) : "--", 0xFFFF, 1);
}

void renderMotionDynamicOnly() {
  if (!mpuReady || !mpuData.valid) {
    drawMpuOfflineDynamic();
    return;
  }
  tftFillRect(58, 28, 58, 85, 0x0841);
  drawText(60, 30, mpuGestureStatus, mpuGestureStatus == "STILL" ? 0x07E0 : 0xFFFF, 1);
  drawText(65, 55, "G", 0xF81F, 1);
  drawText(78, 55, displayNumber(mpuGyroMagnitude), 0xFFFF, 1);
  drawText(66, 80, "UP", 0x07E0, 1);
  drawText(88, 80, String(raiseCount), 0xFFFF, 1);
  drawText(77, 103, "PK" + displayNumber(mpuPeakAccel), 0x9CF3, 1);
  tftFillRect(24, 53, 32, 58, 0x0841);
  drawText(25, 55, displayNumber(mpuAccelMagnitude), 0xFFFF, 1);
  drawText(38, 80, String(shakeCount), 0xFFFF, 1);
  drawText(50, 103, String(fallCount), 0xFFFF, 1);
  tftFillRect(76, 101, 42, 10, 0x0841);
  drawText(77, 103, mpuLedFeedback ? "LED ON" : "LED OFF", mpuLedFeedback ? 0x07E0 : 0x9CF3, 1);
}

void renderSensorDynamicOnly() {
  const SensorPageId page = currentSensorPage();
  if (page == SensorPageId::None) return;
  if (page != lastDynamicSensorPage) {
    renderWeather();
    return;
  }
  if (page == SensorPageId::Mpu) renderMpuDynamicOnly();
  else if (page == SensorPageId::Level) renderLevelDynamicOnly();
  else if (page == SensorPageId::Angle) renderAngleDynamicOnly();
  else if (page == SensorPageId::Odometer) renderOdometerDynamicOnly();
  else if (page == SensorPageId::Motion) renderMotionDynamicOnly();
}

void drawLedSwatch(uint8_t x, uint8_t y, uint16_t color, const char *pin, const char *label, bool active) {
  drawPanel(x, y, 55, 28, active ? 0x2104 : 0x0841, active ? 0xFFFF : 0x2945);
  drawFilledCircle(x + 12, y + 14, 7, color);
  drawText(x + 24, y + 5, pin, 0x9CF3, 1);
  drawText(x + 24, y + 16, label, active ? 0xFFFF : color, 1);
}

uint16_t currentLed565() {
  return ((ledRed & 0xF8) << 8) | ((ledGreen & 0xFC) << 3) | (ledBlue >> 3);
}

void renderLightFrame() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawDashboardHeader("LED", 0x07FF);
  drawText(6, 84, "P6 BREATH P7 FLASH", 0xFFE0, 1);
}

void renderLightDynamicOnly(bool force) {
  const bool changed = force ||
      lastLightRenderedMode != static_cast<uint8_t>(ledMode) ||
      lastLightRenderedRed != ledRed ||
      lastLightRenderedGreen != ledGreen ||
      lastLightRenderedBlue != ledBlue;
  if (!changed) return;
  const bool off = ledRed == 0 && ledGreen == 0 && ledBlue == 0;
  tftFillRect(36, 24, 56, 58, 0x0000);
  drawFilledCircle(64, 47, 21, currentLed565());
  drawFilledCircle(64, 47, 14, off ? 0x0841 : 0xFFFF);
  if (ledMode == LedMode::Rainbow) {
    drawText(42, 73, "RAINBOW", 0x07FF, 1);
  } else if (ledMode == LedMode::Breathe) {
    drawText(43, 73, "BREATHE", 0x07E0, 1);
  } else if (ledMode == LedMode::Flash) {
    drawText(46, 73, "FLASH", 0xF800, 1);
  } else {
    drawText(42, 73, "LIGHT OFF", 0x9CF3, 1);
  }
  drawLedSwatch(6, 94, 0x0000, "P4", "OFF", ledMode == LedMode::Off);
  drawLedSwatch(66, 94, 0x07FF, "P5", "RAIN", ledMode == LedMode::Rainbow);
  lastLightRenderedMode = static_cast<uint8_t>(ledMode);
  lastLightRenderedRed = ledRed;
  lastLightRenderedGreen = ledGreen;
  lastLightRenderedBlue = ledBlue;
}

void renderLightPage() {
  if (!lightPageRendered) {
    renderLightFrame();
    lightPageRendered = true;
    renderLightDynamicOnly(true);
    return;
  }
  renderLightDynamicOnly(false);
}

void lvglSetLabel(lv_obj_t *label, const String &text) {
  if (label) lv_label_set_text(label, text.c_str());
}

lv_obj_t *createWatchLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_letter_space(label, 0, 0);
  return label;
}

lv_obj_t *createOverlayPlate(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_opa_t opacity) {
  lv_obj_t *plate = lv_obj_create(parent);
  lv_obj_remove_style_all(plate);
  lv_obj_set_size(plate, width, height);
  lv_obj_set_style_bg_color(plate, lv_color_hex(0x05070b), 0);
  lv_obj_set_style_bg_opa(plate, opacity, 0);
  lv_obj_set_style_radius(plate, 8, 0);
  lv_obj_set_style_border_width(plate, 1, 0);
  lv_obj_set_style_border_color(plate, lv_color_hex(0x2d3442), 0);
  lv_obj_set_style_border_opa(plate, LV_OPA_50, 0);
  return plate;
}

void styleWatchArc(lv_obj_t *arc, lv_color_t color, int16_t startAngle, int16_t endAngle) {
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_bg_angles(arc, startAngle, endAngle);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x1a1d24), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
}

void buildLvglWatchFace() {
  if (!lvglReady || lvglWatchFaceBuilt) return;
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070b), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  watchRoot = lv_obj_create(screen);
  lv_obj_remove_style_all(watchRoot);
  lv_obj_set_size(watchRoot, kTftWidth, kTftHeight);
  lv_obj_center(watchRoot);
  lv_obj_set_style_bg_color(watchRoot, lv_color_hex(0x05070b), 0);
  lv_obj_set_style_bg_opa(watchRoot, LV_OPA_COVER, 0);

  watchBg = nullptr;

  watchArcTemp = lv_arc_create(watchRoot);
  lv_obj_set_size(watchArcTemp, 118, 118);
  lv_obj_align(watchArcTemp, LV_ALIGN_CENTER, 0, 0);
  styleWatchArc(watchArcTemp, lv_color_hex(0xffc928), 130, 260);

  watchArcMem = lv_arc_create(watchRoot);
  lv_obj_set_size(watchArcMem, 104, 104);
  lv_obj_align(watchArcMem, LV_ALIGN_CENTER, 0, 0);
  styleWatchArc(watchArcMem, lv_color_hex(0x16d6ff), 280, 50);

  watchTimePlate = createOverlayPlate(watchRoot, 64, 57, LV_OPA_50);
  lv_obj_align(watchTimePlate, LV_ALIGN_CENTER, -1, -2);

  watchLabelDate = createWatchLabel(watchRoot, &lv_font_montserrat_8, lv_color_hex(0xd6dbe6));
  lv_obj_align(watchLabelDate, LV_ALIGN_TOP_MID, 0, 8);

  watchLabelHour = createWatchLabel(watchRoot, &lv_font_montserrat_32, lv_color_hex(0xf8fbff));
  lv_obj_align(watchLabelHour, LV_ALIGN_TOP_LEFT, 35, 34);
  watchLabelMinute = createWatchLabel(watchRoot, &lv_font_montserrat_32, lv_color_hex(0x5fe8ff));
  lv_obj_align(watchLabelMinute, LV_ALIGN_TOP_LEFT, 35, 66);

  watchLabelWeather = createWatchLabel(watchRoot, &lv_font_montserrat_10, lv_color_hex(0xffd75a));
  lv_obj_align(watchLabelWeather, LV_ALIGN_TOP_RIGHT, -8, 30);

  watchInfoPlate = createOverlayPlate(watchRoot, 118, 20, LV_OPA_50);
  lv_obj_align(watchInfoPlate, LV_ALIGN_BOTTOM_MID, 0, -4);

  watchBarLoad = lv_bar_create(watchRoot);
  lv_obj_set_size(watchBarLoad, 28, 3);
  lv_obj_align(watchBarLoad, LV_ALIGN_BOTTOM_LEFT, 10, -19);
  lv_bar_set_range(watchBarLoad, 0, 100);
  lv_obj_set_style_radius(watchBarLoad, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(watchBarLoad, 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(watchBarLoad, lv_color_hex(0x202633), LV_PART_MAIN);
  lv_obj_set_style_bg_color(watchBarLoad, lv_color_hex(0x7dff7a), LV_PART_INDICATOR);

  watchBarMem = lv_bar_create(watchRoot);
  lv_obj_set_size(watchBarMem, 28, 3);
  lv_obj_align(watchBarMem, LV_ALIGN_BOTTOM_RIGHT, -10, -19);
  lv_bar_set_range(watchBarMem, 0, 100);
  lv_obj_set_style_radius(watchBarMem, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(watchBarMem, 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(watchBarMem, lv_color_hex(0x202633), LV_PART_MAIN);
  lv_obj_set_style_bg_color(watchBarMem, lv_color_hex(0xff4fd8), LV_PART_INDICATOR);

  watchLabelLoad = createWatchLabel(watchRoot, &lv_font_montserrat_8, lv_color_hex(0x7dff7a));
  lv_obj_align(watchLabelLoad, LV_ALIGN_BOTTOM_LEFT, 10, -7);
  watchLabelMem = createWatchLabel(watchRoot, &lv_font_montserrat_8, lv_color_hex(0xff7ce5));
  lv_obj_align(watchLabelMem, LV_ALIGN_BOTTOM_RIGHT, -10, -7);

  watchLabelNet = createWatchLabel(watchRoot, &lv_font_montserrat_8, lv_color_hex(0xffffff));
  lv_obj_align(watchLabelNet, LV_ALIGN_TOP_LEFT, 8, 30);
  watchLabelAi = createWatchLabel(watchRoot, &lv_font_montserrat_8, lv_color_hex(0xff6b55));
  lv_obj_align(watchLabelAi, LV_ALIGN_BOTTOM_MID, 0, -7);

  lvglWatchFaceBuilt = true;
}

void renderLegacyWatchFace() {
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  drawSportFaceTicks();
  const String now = clockText();

  drawPanel(24, 8, 32, 13, 0x0000, 0xFFE0);
  drawText(30, 12, weather.valid ? displayNumber(weather.temperature) : "--", 0xFFE0, 1);
  drawPanel(70, 8, 34, 13, 0x0000, 0x07FF);
  drawText(77, 12, weather.valid ? String(weather.humidity) + "%" : "--%", 0x07FF, 1);

  drawText(15, 34, now.substring(0, 1), 0x07FF, 4);
  drawText(39, 34, now.substring(1, 2), 0xFFFF, 4);
  drawText(62, 41, ":", 0x9CF3, 3);
  drawText(75, 34, now.substring(3, 4), 0xFFE0, 4);
  drawText(99, 34, now.substring(4, 5), 0xF800, 4);

  drawText(30, 72, dateText(), 0xFFFF, 1);
  drawText(72, 72, "P6 MENU", 0x07E0, 1);

  drawPanel(9, 86, 34, 21, 0x0000, 0x07FF);
  drawText(16, 90, "LOAD", 0x07FF, 1);
  drawText(15, 99, serverStatus.valid ? displayNumber(serverStatus.load1m) : "--", 0xFFFF, 1);
  drawPanel(47, 86, 34, 21, 0x0000, 0xFFFF);
  drawText(57, 90, "MEM", 0x9CF3, 1);
  drawText(54, 99, serverStatus.valid ? String(static_cast<int>(serverStatus.memoryUsedPercent)) : "--", 0xFFFF, 1);
  drawPanel(85, 86, 34, 21, 0x0000, 0xF800);
  drawText(96, 90, "AI", 0xF800, 1);
  drawText(94, 99, "P7", 0xFFFF, 1);

  drawPanel(31, 111, 66, 12, 0x0000, 0xFFE0);
  drawText(42, 115, WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE", 0xFFE0, 1);
}

void renderWatchFace() {
  if (!lvglReady) {
    renderLegacyWatchFace();
    return;
  }
  buildLvglWatchFace();
  const String now = clockText();
  const String hour = now.length() >= 5 ? now.substring(0, 2) : "--";
  const String minute = now.length() >= 5 ? now.substring(3, 5) : "--";
  const int tempPercent = weather.valid && isfinite(weather.temperature) ?
      constrain(static_cast<int>((weather.temperature + 10.0f) * 2.0f), 0, 100) : 0;
  const int memPercent = serverStatus.valid && isfinite(serverStatus.memoryUsedPercent) ?
      constrain(static_cast<int>(serverStatus.memoryUsedPercent), 0, 100) : 0;
  const int cpuPercent = serverStatus.valid && serverStatus.cpuCount > 0 ?
      constrain(static_cast<int>(serverStatus.load1m * 100.0f / serverStatus.cpuCount), 0, 100) : 0;

  lv_arc_set_value(watchArcTemp, tempPercent);
  lv_arc_set_value(watchArcMem, memPercent);
  lvglSetLabel(watchLabelHour, hour);
  lvglSetLabel(watchLabelMinute, minute);
  lvglSetLabel(watchLabelDate, dateText());
  lvglSetLabel(watchLabelWeather, weather.valid ?
      String(displayNumber(weather.temperature)) + "C\n" + String(weather.humidity) + "%" : String("--C\n--%"));
  lvglSetLabel(watchLabelNet, WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
  lvglSetLabel(watchLabelLoad, serverStatus.valid ? String("CPU ") + String(cpuPercent) : String("CPU --"));
  lvglSetLabel(watchLabelMem, serverStatus.valid ? String("MEM ") + String(memPercent) : String("MEM --"));
  lvglSetLabel(watchLabelAi, "P7");
  lv_bar_set_value(watchBarLoad, cpuPercent, LV_ANIM_OFF);
  lv_bar_set_value(watchBarMem, memPercent, LV_ANIM_OFF);
  lv_obj_invalidate(watchRoot);
  lv_refr_now(nullptr);
}

void renderWeather() {
  if (uiPageIs(UiPage::Menu)) {
    renderMenuPage();
    return;
  }
  if (uiPageIs(UiPage::Voice) || voiceResultHeld()) {
    renderVoicePage();
    return;
  }
  if (uiPageIs(UiPage::Light)) {
    renderLightPage();
    return;
  }
  if (uiPageIs(UiPage::Settings)) {
    renderSettingsPage();
    return;
  }
  if (uiPageIs(UiPage::WifiSetup)) {
    renderWifiSetupPage();
    return;
  }
  if (uiPageIs(UiPage::MpuData)) {
    renderMpuPage();
    return;
  }
  if (uiPageIs(UiPage::MpuLevel)) {
    renderLevelPage();
    return;
  }
  if (uiPageIs(UiPage::MpuAngle)) {
    renderAnglePage();
    return;
  }
  if (uiPageIs(UiPage::MpuOdometer)) {
    renderOdometerPage();
    return;
  }
  if (uiPageIs(UiPage::MpuMotion)) {
    renderMotionPage();
    return;
  }
  if (uiPageIs(UiPage::Server)) {
    renderServerStatus();
    return;
  }
  renderWatchFace();
  return;
  tftFillRect(0, 0, kTftWidth, kTftHeight, 0x0000);
  if (!weather.valid) {
    drawZhText(2, 2, "天气连接失败", 0xF800);
    drawText(2, 26, weatherStatus, 0xFFFF, 1);
    return;
  }
  drawDashboardHeader("天气", 0x07E0);
  drawFilledCircle(42, 48, 25, 0x0841);
  drawFilledCircle(42, 48, 23, 0x1082);
  drawZhText(27, 25, "温度", 0xFFE0);
  drawText(14, 43, displayNumber(weather.temperature), 0xFFFF, 3);
  drawText(76, 55, "C", 0x9CF3, 1);
  drawPanel(86, 28, 37, 36, 0x0841, 0x2945);
  drawZhText(92, 31, "体感", 0x07FF);
  drawText(93, 50, displayNumber(weather.apparentTemperature), 0xFFFF, 1);
  tftFillRect(7, 78, 114, 1, 0x2945);
  drawZhText(8, 86, "湿度", 0x07FF);
  drawText(9, 105, String(weather.humidity) + "%", 0xFFFF, 1);
  drawZhText(50, 86, "风速", 0xF81F);
  drawText(50, 105, displayNumber(weather.windSpeed), 0xFFFF, 1);
  drawZhText(92, 86, "降水", 0xFFE0);
  drawText(92, 105, displayNumber(weather.precipitation), 0xFFFF, 1);
  drawText(4, 119, updateTimeText(), 0x9CF3, 1);
}

bool loadServiceCredentials(String &username, String &password) {
  preferences.begin(kServiceNamespace, true);
  username = preferences.getString("username", "");
  password = preferences.getString("password", "");
  preferences.end();
  return !username.isEmpty() && !password.isEmpty();
}

bool loginDudServer() {
  String username;
  String password;
  if (!loadServiceCredentials(username, password)) {
    weatherStatus = "NO SERVICE";
    return false;
  }
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(kAuthUrl)) return false;
  http.addHeader("Content-Type", "application/json");
  const String body = String("{\"username\":\"") + jsonEscape(username) + "\",\"password\":\"" + jsonEscape(password) + "\"}";
  const int code = http.POST(body);
  const String response = http.getString();
  http.end();
  accessToken = code == HTTP_CODE_OK ? jsonString(response, "access_token") : "";
  weatherStatus = accessToken.isEmpty() ? String("LOGIN ") + code : "LOGIN OK";
  return !accessToken.isEmpty();
}

bool parseWeather(const String &body) {
  const String current = jsonObject(body, "current");
  if (current.isEmpty()) return false;
  weather.temperature = jsonNumber(current, "temperature_c", jsonNumber(current, "temperature_2m"));
  weather.apparentTemperature = jsonNumber(current, "apparent_temperature_c", jsonNumber(current, "apparent_temperature"));
  weather.humidity = static_cast<int>(jsonNumber(current, "humidity_percent", jsonNumber(current, "relative_humidity_2m", -1)));
  weather.weatherCode = static_cast<int>(jsonNumber(current, "weather_code", -1));
  weather.windSpeed = jsonNumber(current, "wind_speed_kmh", jsonNumber(current, "wind_speed_10m"));
  weather.precipitation = jsonNumber(current, "precipitation");
  weather.updatedAt = jsonString(current, "time", "--:--");
  weather.valid = true;
  return true;
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherStatus = "NO WIFI";
    return false;
  }
  if (accessToken.isEmpty() && !loginDudServer()) return false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(kWeatherUrl)) return false;
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    const int code = http.GET();
    const String response = code == HTTP_CODE_OK ? http.getString() : "";
    http.end();
    if (code == HTTP_CODE_OK) {
      weather.valid = parseWeather(response);
      weatherStatus = weather.valid ? "DUD WEATHER" : "PARSE ERR";
      return weather.valid;
    }
    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (!loginDudServer()) return false;
      continue;
    }
    weatherStatus = String("HTTP ") + code;
    return false;
  }
  return false;
}

bool fetchServerStatus() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (accessToken.isEmpty() && !loginDudServer()) return false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(kServerStatusUrl)) return false;
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    const int code = http.GET();
    const String body = code == HTTP_CODE_OK ? http.getString() : "";
    http.end();
    if (code == HTTP_CODE_OK) {
      serverStatus.uptimeSeconds = static_cast<int>(jsonNumber(body, "uptime_seconds", 0));
      serverStatus.cpuCount = static_cast<int>(jsonNumber(body, "cpu_count", 0));
      serverStatus.load1m = jsonNumber(body, "load_1m");
      serverStatus.memoryUsedPercent = jsonNumber(body, "memory_used_percent");
      serverStatus.diskUsedPercent = jsonNumber(body, "disk_used_percent");
      serverStatus.sampledAt = jsonString(body, "sampled_at");
      serverStatus.valid = true;
      return true;
    }
    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (!loginDudServer()) return false;
      continue;
    }
    return false;
  }
  return false;
}

bool serviceBaseConfigured() {
  return String(SERVICE_BASE_URL).indexOf("example.invalid") < 0;
}

bool firmwareVersionIsNewer(const String &version) {
  return !version.isEmpty() && version != FIRMWARE_VERSION;
}

bool applyOtaFirmware(const String &firmwareUrl) {
  if (firmwareUrl.isEmpty()) {
    otaStatus = "OTA NOURL";
    return false;
  }

  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(firmwareUrl)) {
    otaStatus = "OTA URL";
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    otaStatus = String("OTA HTTP ") + code;
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    otaStatus = "OTA SPACE";
    http.end();
    return false;
  }

  otaStatus = "OTA WRITE";
  if (uiPageIs(UiPage::Settings)) renderWeather();
  WiFiClient *stream = http.getStreamPtr();
  const size_t written = Update.writeStream(*stream);
  const bool writeOk = contentLength < 0 || written == static_cast<size_t>(contentLength);
  const bool updateOk = writeOk && Update.end() && Update.isFinished();
  http.end();
  if (!updateOk) {
    otaStatus = "OTA FAIL";
    Update.abort();
    return false;
  }

  otaStatus = "OTA REBOOT";
  if (uiPageIs(UiPage::Settings)) renderWeather();
  delay(800);
  ESP.restart();
  return true;
}

bool checkAndApplyOta() {
  if (!serviceBaseConfigured()) {
    otaStatus = "OTA OFF";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    otaStatus = "OTA NOWIFI";
    return false;
  }
  if (accessToken.isEmpty() && !loginDudServer()) {
    otaStatus = "OTA LOGIN";
    return false;
  }

  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(kDeviceConfigUrl)) {
      otaStatus = "OTA BEGIN";
      return false;
    }
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    const int code = http.GET();
    const String body = code == HTTP_CODE_OK ? http.getString() : "";
    http.end();

    if (code == HTTP_CODE_OK) {
      const String firmware = jsonObject(body, "firmware");
      otaAvailableVersion = jsonString(firmware, "version");
      const String firmwareUrl = jsonString(firmware, "url");
      if (!firmwareVersionIsNewer(otaAvailableVersion)) {
        otaStatus = "OTA OK";
        return false;
      }
      otaStatus = String("OTA ") + otaAvailableVersion;
      if (uiPageIs(UiPage::Settings)) renderWeather();
      return applyOtaFirmware(firmwareUrl);
    }

    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (loginDudServer()) continue;
    }
    otaStatus = String("OTA HTTP ") + code;
    return false;
  }
  return false;
}

void writeWavHeader(uint8_t *data, uint32_t pcmBytes) {
  const uint32_t riffSize = 36 + pcmBytes;
  const uint32_t byteRate = kVoiceSampleRate * 2;
  memcpy(data, "RIFF", 4);
  data[4] = riffSize & 0xff;
  data[5] = (riffSize >> 8) & 0xff;
  data[6] = (riffSize >> 16) & 0xff;
  data[7] = (riffSize >> 24) & 0xff;
  memcpy(data + 8, "WAVEfmt ", 8);
  data[16] = 16;
  data[20] = 1;
  data[22] = 1;
  data[24] = kVoiceSampleRate & 0xff;
  data[25] = (kVoiceSampleRate >> 8) & 0xff;
  data[26] = (kVoiceSampleRate >> 16) & 0xff;
  data[27] = (kVoiceSampleRate >> 24) & 0xff;
  data[28] = byteRate & 0xff;
  data[29] = (byteRate >> 8) & 0xff;
  data[30] = (byteRate >> 16) & 0xff;
  data[31] = (byteRate >> 24) & 0xff;
  data[32] = 2;
  data[34] = 16;
  memcpy(data + 36, "data", 4);
  data[40] = pcmBytes & 0xff;
  data[41] = (pcmBytes >> 8) & 0xff;
  data[42] = (pcmBytes >> 16) & 0xff;
  data[43] = (pcmBytes >> 24) & 0xff;
}

bool beginMicrophone() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = kVoiceSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  // Read both slots so the microphone works whether L/R is strapped low or high.
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 4;
  config.dma_buf_len = 128;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
  microphoneDriverInstalled = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = kMicBclkPin;
  pins.ws_io_num = kMicWsPin;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = kMicDataPin;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    microphoneDriverInstalled = false;
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

void endMicrophone() {
  if (!microphoneDriverInstalled) return;
  i2s_driver_uninstall(I2S_NUM_0);
  microphoneDriverInstalled = false;
}

bool beginSpeaker(uint32_t sampleRate) {
  if (microphoneDriverInstalled) endMicrophone();
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = sampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 4;
  config.dma_buf_len = 128;
  config.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
  speakerDriverInstalled = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = kMicBclkPin;
  pins.ws_io_num = kMicWsPin;
  pins.data_out_num = kSpeakerDataPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    speakerDriverInstalled = false;
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

size_t amplifyMonoToStereoPcm16(const uint8_t *input, size_t inputBytes, uint8_t *output, size_t outputBytes) {
  const int16_t *inputSamples = reinterpret_cast<const int16_t *>(input);
  int16_t *outputSamples = reinterpret_cast<int16_t *>(output);
  const size_t inputSampleCount = inputBytes / sizeof(int16_t);
  const size_t maxOutputFrames = outputBytes / (sizeof(int16_t) * 2);
  const size_t frameCount = min(inputSampleCount, maxOutputFrames);
  for (size_t index = 0; index < frameCount; ++index) {
    int32_t sample = static_cast<int32_t>(inputSamples[index]) * speakerVolumePercent / 100;
    sample = constrain(sample, -32768, 32767);
    outputSamples[index * 2] = static_cast<int16_t>(sample);
    outputSamples[index * 2 + 1] = static_cast<int16_t>(sample);
  }
  return frameCount * sizeof(int16_t) * 2;
}

void loadAudioSettings() {
  preferences.begin(kAudioNamespace, true);
  speakerVolumePercent = preferences.getInt("volume", speakerVolumePercent);
  preferences.end();
  speakerVolumePercent = constrain(speakerVolumePercent, kSpeakerVolumeMinPercent, kSpeakerVolumeMaxPercent);
}

void saveAudioSettings() {
  preferences.begin(kAudioNamespace, false);
  preferences.putInt("volume", speakerVolumePercent);
  preferences.end();
}

void adjustSpeakerVolume(int deltaPercent) {
  const int previousVolume = speakerVolumePercent;
  speakerVolumePercent = constrain(
      speakerVolumePercent + deltaPercent,
      kSpeakerVolumeMinPercent,
      kSpeakerVolumeMaxPercent);
  if (speakerVolumePercent != previousVolume) saveAudioSettings();
}

void endSpeaker() {
  if (!speakerDriverInstalled) return;
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
  speakerDriverInstalled = false;
}

void releaseVoiceBuffer() {
  if (voiceWav) free(voiceWav);
  voiceWav = nullptr;
  voicePcmBytes = 0;
  voicePcmCapacity = 0;
}

class MultipartVoiceStream : public Stream {
 public:
  MultipartVoiceStream(const String &prefix, const uint8_t *wav, size_t wavBytes, const String &suffix)
      : prefix_(reinterpret_cast<const uint8_t *>(prefix.c_str())), prefixBytes_(prefix.length()),
        wav_(wav), wavBytes_(wavBytes), suffix_(reinterpret_cast<const uint8_t *>(suffix.c_str())),
        suffixBytes_(suffix.length()) {}

  int available() override { return static_cast<int>(totalBytes() - offset_); }

  int read() override {
    uint8_t byte = 0;
    return readBytes(reinterpret_cast<char *>(&byte), 1) == 1 ? byte : -1;
  }

  int peek() override {
    if (offset_ >= totalBytes()) return -1;
    return byteAt(offset_);
  }

  void flush() override {}
  size_t write(uint8_t) override { return 0; }

  size_t readBytes(char *buffer, size_t length) override {
    const size_t remaining = totalBytes() - offset_;
    size_t copied = 0;
    length = min(length, remaining);
    while (copied < length) {
      const uint8_t *source = nullptr;
      size_t sourceOffset = 0;
      size_t sourceRemaining = 0;
      if (offset_ < prefixBytes_) {
        source = prefix_;
        sourceOffset = offset_;
        sourceRemaining = prefixBytes_ - sourceOffset;
      } else if (offset_ < prefixBytes_ + wavBytes_) {
        source = wav_;
        sourceOffset = offset_ - prefixBytes_;
        sourceRemaining = wavBytes_ - sourceOffset;
      } else {
        source = suffix_;
        sourceOffset = offset_ - prefixBytes_ - wavBytes_;
        sourceRemaining = suffixBytes_ - sourceOffset;
      }
      const size_t chunk = min(length - copied, sourceRemaining);
      memcpy(buffer + copied, source + sourceOffset, chunk);
      copied += chunk;
      offset_ += chunk;
    }
    return copied;
  }

 private:
  size_t totalBytes() const { return prefixBytes_ + wavBytes_ + suffixBytes_; }

  uint8_t byteAt(size_t position) const {
    if (position < prefixBytes_) return prefix_[position];
    if (position < prefixBytes_ + wavBytes_) return wav_[position - prefixBytes_];
    return suffix_[position - prefixBytes_ - wavBytes_];
  }

  const uint8_t *prefix_;
  size_t prefixBytes_;
  const uint8_t *wav_;
  size_t wavBytes_;
  const uint8_t *suffix_;
  size_t suffixBytes_;
  size_t offset_ = 0;
};

uint16_t littleEndian16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t littleEndian32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool readAudioExactly(Stream &stream, uint8_t *data, size_t length) {
  size_t received = 0;
  const unsigned long deadline = millis() + 8000;
  while (received < length && millis() < deadline) {
    const size_t read = stream.readBytes(reinterpret_cast<char *>(data + received), length - received);
    if (read == 0) {
      delay(1);
      continue;
    }
    received += read;
  }
  return received == length;
}

bool skipAudioBytes(Stream &stream, uint32_t bytes) {
  uint8_t discard[128];
  while (bytes > 0) {
    const size_t chunk = min(static_cast<uint32_t>(sizeof(discard)), bytes);
    if (!readAudioExactly(stream, discard, chunk)) return false;
    bytes -= chunk;
  }
  return true;
}

bool playVoiceReply(const String &audioUrl) {
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(audioUrl) || http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  WiFiClient *client = http.getStreamPtr();
  const int httpContentBytes = http.getSize();
  size_t wavHeaderBytes = 0;
  uint8_t riffHeader[12];
  if (!readAudioExactly(*client, riffHeader, sizeof(riffHeader)) ||
      memcmp(riffHeader, "RIFF", 4) != 0 || memcmp(riffHeader + 8, "WAVE", 4) != 0) {
    http.end();
    return false;
  }
  wavHeaderBytes += sizeof(riffHeader);

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint32_t dataBytes = 0;
  bool foundData = false;
  while (!foundData) {
    uint8_t chunkHeader[8];
    if (!readAudioExactly(*client, chunkHeader, sizeof(chunkHeader))) break;
    wavHeaderBytes += sizeof(chunkHeader);
    const uint32_t chunkBytes = littleEndian32(chunkHeader + 4);
    if (memcmp(chunkHeader, "fmt ", 4) == 0) {
      if (chunkBytes < 16) break;
      uint8_t format[16];
      if (!readAudioExactly(*client, format, sizeof(format))) break;
      wavHeaderBytes += sizeof(format);
      audioFormat = littleEndian16(format);
      channels = littleEndian16(format + 2);
      sampleRate = littleEndian32(format + 4);
      bitsPerSample = littleEndian16(format + 14);
      const uint32_t skipped = chunkBytes - 16 + (chunkBytes & 1);
      if (!skipAudioBytes(*client, skipped)) break;
      wavHeaderBytes += skipped;
    } else if (memcmp(chunkHeader, "data", 4) == 0) {
      dataBytes = chunkBytes;
      foundData = true;
    } else {
      const uint32_t skipped = chunkBytes + (chunkBytes & 1);
      if (!skipAudioBytes(*client, skipped)) break;
      wavHeaderBytes += skipped;
    }
  }

  if (httpContentBytes > static_cast<int>(wavHeaderBytes)) {
    dataBytes = min(dataBytes, static_cast<uint32_t>(httpContentBytes - wavHeaderBytes));
  }

  if (!foundData || audioFormat != 1 || channels != 1 || bitsPerSample != 16 ||
      sampleRate < 8000 || sampleRate > 48000 || !beginSpeaker(sampleRate)) {
    http.end();
    return false;
  }

  uint8_t audioBuffer[512];
  uint8_t stereoBuffer[1024];
  uint32_t remaining = dataBytes;
  bool played = false;
  while (remaining > 0) {
    const size_t wanted = min(static_cast<uint32_t>(sizeof(audioBuffer)), remaining);
    const size_t received = client->readBytes(reinterpret_cast<char *>(audioBuffer), wanted);
    if (received == 0) break;
    const size_t stereoBytes = amplifyMonoToStereoPcm16(audioBuffer, received, stereoBuffer, sizeof(stereoBuffer));
    if (stereoBytes == 0) break;
    size_t written = 0;
    if (i2s_write(I2S_NUM_0, stereoBuffer, stereoBytes, &written, pdMS_TO_TICKS(1000)) != ESP_OK ||
        written != stereoBytes) break;
    remaining -= received;
    played = true;
  }
  endSpeaker();
  http.end();
  return played;
}

bool startVoiceStreamSession() {
  voiceStreamActive = false;
  voiceStreamFailed = false;
  voiceStreamUploadedBytes = 0;
  voiceStreamSessionId = "";
  if (WiFi.status() != WL_CONNECTED) return false;
  if (accessToken.isEmpty() && !loginDudServer()) return false;

  const String deviceId = String("esp32s3-") + WiFi.macAddress();
  const String body = String("device_id=") + deviceId + "&source=ai_chat_fresh";
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(kVoiceStreamStartUrl)) return false;
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int code = http.POST(body);
    const String response = http.getString();
    http.end();
    if (code == HTTP_CODE_OK) {
      voiceStreamSessionId = jsonString(response, "session_id");
      voiceStreamActive = !voiceStreamSessionId.isEmpty();
      return voiceStreamActive;
    }
    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (loginDudServer()) continue;
    }
    Serial.printf("Voice stream start failed: HTTP %d %s\n", code, response.c_str());
    return false;
  }
  return false;
}

bool sendVoiceStreamChunk(const uint8_t *data, size_t bytes) {
  if (!voiceStreamActive || voiceStreamFailed || bytes == 0) return false;
  const String url = String(kVoiceStreamBaseUrl) + voiceStreamSessionId + "/chunk";
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(url)) return false;
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    http.addHeader("Content-Type", "application/octet-stream");
    const int code = http.POST(const_cast<uint8_t *>(data), bytes);
    const String response = http.getString();
    http.end();
    if (code == HTTP_CODE_OK) {
      voiceStreamUploadedBytes += bytes;
      return true;
    }
    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (loginDudServer()) continue;
    }
    Serial.printf("Voice stream chunk failed: HTTP %d %s\n", code, response.c_str());
    voiceStreamFailed = true;
    voiceStreamActive = false;
    return false;
  }
  return false;
}

void stopVoiceCallMode() {
  voiceCallMode = false;
  voiceCallRestartMs = 0;
  voiceSpeechDetected = false;
  voiceStopRequested = true;
}

void startVoiceRecording(bool callMode = false) {
  setUiPage(UiPage::Voice);
  voiceResultHoldUntilMs = 0;
  voiceUploadPending = false;
  voiceStopRequested = false;
  voiceCallMode = callMode || voiceCallMode;
  voiceCallRestartMs = 0;
  voiceSpeechDetected = false;
  voiceTranscript = "";
  voiceReply = "";
  voiceReplyBitmapValid = false;
  voiceStreamActive = false;
  voiceStreamFailed = false;
  voiceStreamUploadedBytes = 0;
  voiceStreamSessionId = "";
  releaseVoiceBuffer();

  voicePcmCapacity = kVoiceMaxPcmBytes;
  voiceWav = static_cast<uint8_t *>(ps_malloc(44 + voicePcmCapacity));
  if (!voiceWav) {
    voicePcmCapacity = kVoiceFallbackPcmBytes;
    voiceWav = static_cast<uint8_t *>(malloc(44 + voicePcmCapacity));
  }
  if (!voiceWav) {
    voiceStatus = "MEM";
    renderWeather();
    return;
  }
  memset(voiceWav, 0, 44);
  writeWavHeader(voiceWav, 0);
  startVoiceStreamSession();
  if (!beginMicrophone()) {
    releaseVoiceBuffer();
    voiceStatus = "ERR";
    renderWeather();
    return;
  }

  voicePcmBytes = 0;
  voiceRecordStartedMs = millis();
  voiceLastSoundMs = voiceRecordStartedMs;
  voiceRecording = true;
  voiceStatus = voiceCallMode ? "CALL" : "REC";
  renderWeather();
  Serial.printf("Voice recording started, capacity=%u bytes\n", static_cast<unsigned>(voicePcmCapacity));
}

bool handleVoiceAiResponse(bool success, int code, const String &response) {
  if (!success) {
    if (code == 422 && response.indexOf("audio too quiet") >= 0) {
      voiceStatus = "NOSOUND";
    } else if (code == 422 && response.indexOf("no speech detected") >= 0) {
      voiceStatus = "NOSPEECH";
    } else {
      voiceStatus = code == 422 ? "SHORT" : "ERR";
    }
    setUiPage(UiPage::Voice);
    voiceResultHoldUntilMs = voiceCallMode ? 0 : millis() + kVoiceResultHoldMs;
    if (voiceCallMode) voiceCallRestartMs = millis() + kVoiceCallRestartDelayMs;
    Serial.printf("Voice AI request failed: HTTP %d %s\n", code, response.c_str());
    return false;
  }
  voiceTranscript = jsonString(response, "transcript_text");
  voiceReply = jsonString(response, "reply_text");
  voiceReplyBitmapValid = decodeVoiceReplyBitmap(jsonString(response, "reply_bitmap_1bpp"));
  voiceStatus = voiceTranscript.isEmpty() ? "ERR" : "OK";
  const String audioUrl = jsonString(response, "audio_url");
  Serial.printf("Voice transcript: %s\nAI reply: %s\n", voiceTranscript.c_str(), voiceReply.c_str());
  if (voiceStatus == "OK" && !audioUrl.isEmpty()) {
    voiceStatus = "SPEAK";
    renderWeather();
    if (!playVoiceReply(audioUrl)) Serial.println("Voice playback failed");
    voiceStatus = "OK";
  }
  setUiPage(UiPage::Voice);
  voiceResultHoldUntilMs = voiceCallMode ? 0 : millis() + kVoiceResultHoldMs;
  if (voiceCallMode) voiceCallRestartMs = millis() + kVoiceCallRestartDelayMs;
  return voiceStatus == "OK";
}

bool finishVoiceStreamSession() {
  if (!voiceStreamActive || voiceStreamFailed || voiceStreamSessionId.isEmpty()) return false;
  if (voicePcmBytes > voiceStreamUploadedBytes) {
    const size_t remaining = voicePcmBytes - voiceStreamUploadedBytes;
    if (!sendVoiceStreamChunk(voiceWav + 44 + voiceStreamUploadedBytes, remaining)) return false;
  }

  const String url = String(kVoiceStreamBaseUrl) + voiceStreamSessionId + "/finish";
  bool success = false;
  bool terminal = false;
  String response;
  int code = 0;
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(45000);
    if (!http.begin(url)) break;
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    code = http.POST("");
    response = http.getString();
    http.end();
    if (code == HTTP_CODE_OK) {
      success = true;
      terminal = true;
      break;
    }
    if (code == 422) {
      terminal = true;
      break;
    }
    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (loginDudServer()) continue;
    }
    Serial.printf("Voice stream finish failed: HTTP %d %s\n", code, response.c_str());
    break;
  }

  if (!terminal) return false;
  voiceStreamActive = false;
  voiceStreamSessionId = "";
  releaseVoiceBuffer();
  handleVoiceAiResponse(success, code, response);
  return true;
}

bool uploadVoiceRecording() {
  voiceUploadPending = false;
  if (WiFi.status() != WL_CONNECTED) {
    voiceStatus = "NOWIFI";
    return false;
  }
  if (accessToken.isEmpty() && !loginDudServer()) {
    voiceStatus = "ERR";
    return false;
  }

  const String boundary = "ESP32S3VoiceBoundary";
  const String deviceId = String("esp32s3-") + WiFi.macAddress();
  const String prefix = String("--") + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n" + deviceId + "\r\n--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"source\"\r\n\r\nai_chat_fresh\r\n--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n"
      "Content-Type: audio/wav\r\n\r\n";
  const String suffix = String("\r\n--") + boundary + "--\r\n";
  const size_t wavBytes = 44 + voicePcmBytes;
  const size_t payloadBytes = prefix.length() + wavBytes + suffix.length();

  bool success = false;
  String response;
  int code = 0;
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    http.setTimeout(45000);
    if (!http.begin(kVoiceUploadUrl)) break;
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);
    MultipartVoiceStream stream(prefix, voiceWav, wavBytes, suffix);
    code = http.sendRequest("POST", &stream, payloadBytes);
    response = http.getString();
    http.end();
    if (code == HTTP_CODE_OK) {
      success = true;
      break;
    }
    if (code == 401 && attempt == 0) {
      accessToken = "";
      if (loginDudServer()) continue;
    }
    break;
  }
  releaseVoiceBuffer();
  return handleVoiceAiResponse(success, code, response);
}

void finishVoiceRecording() {
  voiceRecording = false;
  endMicrophone();
  writeWavHeader(voiceWav, static_cast<uint32_t>(voicePcmBytes));
  if (voicePcmBytes < kVoiceMinPcmBytes) {
    releaseVoiceBuffer();
    voiceStatus = voiceCallMode ? "LISTEN" : "SHORT";
    if (voiceCallMode) voiceCallRestartMs = millis() + kVoiceCallRestartDelayMs;
    renderWeather();
    return;
  }
  voiceStatus = "UP";
  voiceUploadPending = true;
  renderWeather();
}

void serviceVoiceRecording() {
  if (voiceUploadPending) {
    if (!finishVoiceStreamSession()) uploadVoiceRecording();
    renderWeather();
    return;
  }
  if (!voiceRecording) return;

  int32_t rawSamples[128];
  size_t bytesRead = 0;
  const esp_err_t result = i2s_read(I2S_NUM_0, rawSamples, sizeof(rawSamples), &bytesRead, pdMS_TO_TICKS(15));
  if (result != ESP_OK) {
    voiceStatus = "ERR";
    voiceRecording = false;
    endMicrophone();
    releaseVoiceBuffer();
    renderWeather();
    return;
  }
  const size_t samplesRead = bytesRead / sizeof(int32_t);
  const size_t availableSamples = (voicePcmCapacity - voicePcmBytes) / sizeof(int16_t);
  const size_t samplesToStore = min(samplesRead / 2, availableSamples);
  int16_t *pcm = reinterpret_cast<int16_t *>(voiceWav + 44 + voicePcmBytes);
  int32_t maxLevel = 0;
  for (size_t index = 0; index < samplesToStore; ++index) {
    const int32_t first = rawSamples[index * 2];
    const int32_t second = rawSamples[index * 2 + 1];
    const int64_t firstMagnitude = first < 0 ? -static_cast<int64_t>(first) : first;
    const int64_t secondMagnitude = second < 0 ? -static_cast<int64_t>(second) : second;
    int32_t sample = (firstMagnitude >= secondMagnitude ? first : second) >> 14;
    sample = constrain(sample, -32768, 32767);
    pcm[index] = static_cast<int16_t>(sample);
    const int32_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude > maxLevel) maxLevel = magnitude;
  }
  voicePcmBytes += samplesToStore * sizeof(int16_t);
  if (maxLevel >= kVoiceSpeechLevel) {
    voiceSpeechDetected = true;
    voiceLastSoundMs = millis();
  }

  if (voiceStreamActive && !voiceStreamFailed &&
      voicePcmBytes >= voiceStreamUploadedBytes + kVoiceStreamChunkPcmBytes) {
    sendVoiceStreamChunk(voiceWav + 44 + voiceStreamUploadedBytes, kVoiceStreamChunkPcmBytes);
  }

  const unsigned long elapsedMs = millis() - voiceRecordStartedMs;
  const bool timedOut = elapsedMs >= kVoiceMaxRecordMs;
  const bool autoSpeechEnded = voiceCallMode && voiceSpeechDetected &&
      elapsedMs >= kVoiceCallMinRecordMs &&
      millis() - voiceLastSoundMs >= kVoiceCallSilenceMs;
  const bool autoNoSpeechReset = voiceCallMode && !voiceSpeechDetected &&
      elapsedMs >= kVoiceCallNoSpeechRestartMs;
  if (autoNoSpeechReset) {
    voiceRecording = false;
    endMicrophone();
    releaseVoiceBuffer();
    voiceStatus = "LISTEN";
    voiceCallRestartMs = millis() + kVoiceCallRestartDelayMs;
    renderWeather();
    return;
  }
  if (voiceStopRequested || timedOut || autoSpeechEnded) {
    finishVoiceRecording();
    return;
  }

  static unsigned long lastRenderedSecond = static_cast<unsigned long>(-1);
  const unsigned long second = (millis() - voiceRecordStartedMs) / 1000UL;
  if (second != lastRenderedSecond) {
    lastRenderedSecond = second;
    renderWeather();
  }
}

void serviceVoiceCallAutoRestart() {
  if (!voiceCallMode || voiceRecording || voiceUploadPending || voiceStatus == "SPEAK") return;
  if (voiceCallRestartMs == 0 || millis() < voiceCallRestartMs) return;
  voiceCallRestartMs = 0;
  startVoiceRecording(true);
}

void serviceWeather() {
  if (voiceRecording || voiceUploadPending || voiceStatus == "SPEAK" || voiceResultHeld()) return;
  if (millis() < nextWeatherFetchMs) return;
  const bool wasValid = weather.valid;
  if (fetchWeather()) {
    nextWeatherFetchMs = millis() + kWeatherRefreshMs;
  } else {
    nextWeatherFetchMs = millis() + (WiFi.status() == WL_CONNECTED ? kFastRetryMs : kSlowNetworkRetryMs);
    if (!wasValid) weather.valid = false;
  }
  renderWeather();
}

void serviceServerStatus() {
  if (voiceRecording || voiceUploadPending || voiceStatus == "SPEAK" || voiceResultHeld()) return;
  if (millis() < nextServerFetchMs) return;
  nextServerFetchMs = millis() + (fetchServerStatus() ? kServerRefreshMs :
      (WiFi.status() == WL_CONNECTED ? kFastRetryMs : kSlowNetworkRetryMs));
  renderWeather();
}

void serviceOta() {
  if (voiceRecording || voiceUploadPending || voiceStatus == "SPEAK" || voiceResultHeld()) return;
  if (millis() < nextOtaCheckMs) return;
  nextOtaCheckMs = millis() + kOtaCheckMs;
  checkAndApplyOta();
  if (uiPageIs(UiPage::Settings)) renderWeather();
}

void serviceDashboardPage() {
  if (!(uiPageIs(UiPage::Watch) || uiPageIs(UiPage::Server)) || voiceResultHeld()) return;
  if (!autoRotatePages) return;
  if (millis() < nextDashboardPageMs) return;
  setUiPage(uiPageIs(UiPage::Server) ? UiPage::Watch : UiPage::Server);
  nextDashboardPageMs = millis() + kDashboardPageMs;
  renderWeather();
}

void serviceWatchFace() {
  if (!watchFaceVisible()) return;
  if (millis() < nextWatchFaceRefreshMs) return;
  nextWatchFaceRefreshMs = millis() + 10000UL;
  renderWeather();
}

void serviceMpuPage() {
  if (!mpuReady) {
    retryMpu6050();
    if ((sensorPageVisible()) &&
        millis() >= nextMpuRefreshMs) {
      nextMpuRefreshMs = millis() + 500UL;
      renderSensorDynamicOnly();
    }
    return;
  }
  if (millis() >= nextMpuGestureMs) {
    nextMpuGestureMs = millis() + kMpuGestureMs;
    readMpuData();
    serviceMpuGestures();
    serviceOdometer();
  }
  if (!(sensorPageVisible()) ||
      millis() < nextMpuRefreshMs) return;
  nextMpuRefreshMs = millis() + kMpuRefreshMs;
  renderSensorDynamicOnly();
}

void serviceLvgl() {
  if (!lvglReady) return;
  static unsigned long lastTickMs = 0;
  const unsigned long now = millis();
  if (lastTickMs == 0) lastTickMs = now;
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;
  if (watchFaceVisible()) lv_timer_handler();
}

void closeAllPages() {
  setUiPage(UiPage::Watch);
}

void openMenuItem(MenuItem item) {
  voiceResultHoldUntilMs = 0;
  closeAllPages();
  if (item == MenuItem::Settings) {
    setUiPage(UiPage::Settings);
    nextWatchFaceRefreshMs = 0;
  } else if (item == MenuItem::Server) {
    setUiPage(UiPage::Server);
  } else if (item == MenuItem::Light) {
    setUiPage(UiPage::Light);
  } else if (item == MenuItem::Mpu) {
    setUiPage(UiPage::MpuData);
    readMpuData();
    nextMpuRefreshMs = 0;
  } else if (item == MenuItem::Angle) {
    setUiPage(UiPage::MpuAngle);
    readMpuData();
    nextMpuRefreshMs = 0;
  } else if (item == MenuItem::Level) {
    setUiPage(UiPage::MpuLevel);
    readMpuData();
    nextMpuRefreshMs = 0;
  } else if (item == MenuItem::Odometer) {
    setUiPage(UiPage::MpuOdometer);
    readMpuData();
    nextMpuRefreshMs = 0;
  } else if (item == MenuItem::Motion) {
    setUiPage(UiPage::MpuMotion);
    readMpuData();
    nextMpuRefreshMs = 0;
  } else if (!voiceUploadPending) {
    startVoiceRecording(true);
  }
}

void exitVoicePage() {
  stopVoiceCallMode();
  voiceUploadPending = false;
  voiceStreamActive = false;
  voiceStreamFailed = false;
  voiceStreamSessionId = "";
  releaseVoiceBuffer();
  voiceResultHoldUntilMs = 0;
  closeAllPages();
  nextWatchFaceRefreshMs = 0;
}

void dispatchVoiceRecordingButton(size_t index) {
  if (index == 0) {
    stopVoiceCallMode();
    voiceRecording = false;
    voiceUploadPending = false;
    endMicrophone();
    releaseVoiceBuffer();
    closeAllPages();
    voiceStatus = "READY";
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    adjustSpeakerVolume(-kSpeakerVolumeStepPercent);
  } else if (index == 2) {
    adjustSpeakerVolume(kSpeakerVolumeStepPercent);
  } else if (index == 3) {
    voiceStopRequested = true;
  }
  renderWeather();
}

void dispatchVoicePageButton(size_t index) {
  if (index == 0) {
    exitVoicePage();
  } else if (index == 1) {
    adjustSpeakerVolume(-kSpeakerVolumeStepPercent);
  } else if (index == 2) {
    adjustSpeakerVolume(kSpeakerVolumeStepPercent);
  } else if (!voiceUploadPending && voiceStatus != "SPEAK") {
    startVoiceRecording(true);
    return;
  }
  renderWeather();
}

void dispatchMenuButton(size_t index) {
  voiceResultHoldUntilMs = 0;
  if (index == 0) {
    closeAllPages();
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    menuSelectedIndex = menuSelectedIndex == 0 ? kMenuEntryCount - 1 : menuSelectedIndex - 1;
  } else if (index == 2) {
    menuSelectedIndex = (menuSelectedIndex + 1) % kMenuEntryCount;
  } else {
    openMenuItem(kMenuEntries[menuSelectedIndex].item);
    if (voiceRecording || voiceUploadPending) return;
  }
  nextDashboardPageMs = millis() + kDashboardPageMs;
  renderWeather();
}

void dispatchSettingsButton(size_t index) {
  if (index == 0) {
    closeAllPages();
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    adjustSpeakerVolume(-kSpeakerVolumeStepPercent);
  } else if (index == 2) {
    adjustSpeakerVolume(kSpeakerVolumeStepPercent);
  } else {
    setUiPage(UiPage::WifiSetup);
    startConfigurationPortal(true);
    nextWatchFaceRefreshMs = 0;
  }
  renderWeather();
}

void dispatchWifiSetupButton(size_t index) {
  if (index == 0) setUiPage(UiPage::Settings);
  renderWeather();
}

void dispatchMpuButton(size_t index) {
  if (index == 0) {
    closeAllPages();
    nextWatchFaceRefreshMs = 0;
  } else if (index == 3) {
    calibrateMpuZero();
  }
  renderWeather();
}

void dispatchOdometerButton(size_t index) {
  if (index == 0) {
    closeAllPages();
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    odometerSteps = 0;
    lastOdometerAccel = NAN;
    lastOdometerStepMs = 0;
  } else if (index == 3) {
    calibrateMpuZero();
    odometerSteps = 0;
    lastOdometerAccel = NAN;
  }
  renderWeather();
}

void dispatchMotionButton(size_t index) {
  if (index == 0) {
    closeAllPages();
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    shakeCount = 0;
    raiseCount = 0;
    fallCount = 0;
    mpuPeakAccel = 0.0f;
    mpuPeakGyro = 0.0f;
    mpuGestureStatus = "READY";
  } else if (index == 2) {
    mpuLedFeedback = !mpuLedFeedback;
    saveMpuSettings();
    if (!mpuLedFeedback && !uiPageIs(UiPage::Light) && ledMode == LedMode::Flash) {
      mpuLedPulseUntilMs = 0;
      setLedMode(LedMode::Off);
    }
  } else if (index == 3) {
    calibrateMpuZero();
  }
  renderWeather();
}

void dispatchLightButton(size_t index) {
  mpuLedPulseUntilMs = 0;
  if (index == 0) {
    setLedMode(LedMode::Off);
    closeAllPages();
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    setLedMode(LedMode::Rainbow);
  } else if (index == 2) {
    setLedMode(LedMode::Breathe);
  } else {
    setLedMode(LedMode::Flash);
  }
  renderWeather();
}

void dispatchHomeButton(size_t index) {
  voiceResultHoldUntilMs = 0;
  if (index == 0 || index == 2) {
    closeAllPages();
    setUiPage(UiPage::Menu);
    nextWatchFaceRefreshMs = 0;
  } else if (index == 1) {
    openMenuItem(MenuItem::Server);
  } else if (!voiceUploadPending) {
    closeAllPages();
    startVoiceRecording(true);
    return;
  }
  nextDashboardPageMs = millis() + kDashboardPageMs;
  renderWeather();
}

void dispatchButton(size_t index) {
  if (voiceRecording) {
    dispatchVoiceRecordingButton(index);
  } else if (uiPageIs(UiPage::Voice) || voiceResultHeld()) {
    dispatchVoicePageButton(index);
  } else if (uiPageIs(UiPage::Menu)) {
    dispatchMenuButton(index);
  } else if (uiPageIs(UiPage::Settings)) {
    dispatchSettingsButton(index);
  } else if (uiPageIs(UiPage::WifiSetup)) {
    dispatchWifiSetupButton(index);
  } else if (uiPageIs(UiPage::MpuData) || uiPageIs(UiPage::MpuLevel) || uiPageIs(UiPage::MpuAngle)) {
    dispatchMpuButton(index);
  } else if (uiPageIs(UiPage::MpuOdometer)) {
    dispatchOdometerButton(index);
  } else if (uiPageIs(UiPage::MpuMotion)) {
    dispatchMotionButton(index);
  } else if (uiPageIs(UiPage::Light)) {
    dispatchLightButton(index);
  } else {
    dispatchHomeButton(index);
  }
}

void serviceButtons() {
  for (size_t index = 0; index < 4; ++index) {
    const bool current = digitalRead(kButtonPins[index]);
    if (current != buttonRawState[index]) {
      buttonRawState[index] = current;
      buttonChangedMs[index] = millis();
    }
    if (current == buttonStableState[index] || millis() - buttonChangedMs[index] < kButtonDebounceMs) {
      continue;
    }
    buttonStableState[index] = current;
    if (current == LOW) dispatchButton(index);
  }
}

bool connectToSavedNetwork() {
  preferences.begin(kPreferencesNamespace, true);
  const String ssid = preferences.getString("ssid", "");
  const String password = preferences.getString("password", "");
  preferences.end();

  if (ssid.isEmpty()) {
    return false;
  }

  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  const unsigned long deadline = millis() + 15000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void startConfigurationPortal(bool preserveStation) {
  if (portalActive) return;
  const bool keepStation = preserveStation && WiFi.status() == WL_CONNECTED;
  if (!keepStation) {
    WiFi.disconnect();
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);
  }
  WiFi.mode(keepStation ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP(kApName, nullptr, 6, false, 4);
  dnsServer.start(kDnsPort, "*", WiFi.softAPIP());
  portalActive = true;

  if (!portalRoutesRegistered) {
    server.on("/", HTTP_GET, []() {
      server.send_P(200, "text/html; charset=utf-8", kSetupPage);
    });
    server.on("/status", HTTP_GET, []() {
      const bool connected = WiFi.status() == WL_CONNECTED;
      String body = "{";
      body += "\"connected\":" + String(connected ? "true" : "false");
      body += ",\"ssid\":\"" + jsonEscape(connected ? WiFi.SSID() : String("")) + "\"";
      body += ",\"ip\":\"" + String(connected ? WiFi.localIP().toString() : String("")) + "\"";
      body += ",\"ap\":\"" + String(kApName) + "\"";
      body += ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
      body += "}";
      server.send(200, "application/json; charset=utf-8", body);
    });
    server.on("/save", HTTP_POST, []() {
      const String ssid = server.arg("ssid");
      const String password = server.arg("password");
      if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) {
        server.send(400, "text/plain; charset=utf-8", "Wi-Fi 名称或密码无效");
        return;
      }

      preferences.begin(kPreferencesNamespace, false);
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      preferences.end();
      server.send(200, "text/html; charset=utf-8", "<p>已保存，设备正在重启并连接 Wi-Fi。</p>");
      delay(800);
      ESP.restart();
    });
    server.on("/reset", HTTP_POST, []() {
      preferences.begin(kPreferencesNamespace, false);
      preferences.clear();
      preferences.end();
      server.send(200, "text/plain; charset=utf-8", "配置已清除，设备将重启。");
      delay(500);
      ESP.restart();
    });
    server.onNotFound([]() {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
      server.send(302, "text/plain", "");
    });
    portalRoutesRegistered = true;
  }
  server.begin();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  initializeDisplay();
  initializeLvglDisplay();
  initializeButtons();
  initializeBoardLed();
  initializeMpu6050();
  loadAudioSettings();
#if defined(PROVISION_SSID) && defined(PROVISION_PASSWORD)
  provisionCredentials();
#endif
#if defined(PROVISION_SERVICE_USER) && defined(PROVISION_SERVICE_PASSWORD)
  provisionServiceCredentials();
#endif
  if (connectToSavedNetwork()) {
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    Serial.printf("Connected: %s\n", WiFi.localIP().toString().c_str());
    return;
  }

  startConfigurationPortal();
  Serial.printf("Portal: http://%s\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  serviceBoardLedEffects();
  serviceLvgl();
  if (portalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  serviceButtons();
  serviceMpuPage();
  serviceVoiceRecording();
  serviceVoiceCallAutoRestart();
  if (!voiceRecording && !voiceUploadPending) {
    serviceWeather();
    serviceServerStatus();
    serviceOta();
  }
  serviceDashboardPage();
  serviceWatchFace();
}
