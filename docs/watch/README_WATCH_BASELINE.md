# ESP32-S3-Touch-AMOLED-2.06 官方 Brookesia 基线

这个工程从 Waveshare 官方示例复制而来：

- 官方仓库：`waveshareteam/ESP32-S3-Touch-AMOLED-2.06`
- 基线示例：`examples/esp-idf/03_esp-brookesia`
- 目标板：`ESP32-S3-Touch-AMOLED-2.06`

## 当前策略

继续二次开发时，以这个工程作为新的稳定基线，不再继续在旧工程里大范围修改 ESP-Brookesia 框架核心。

原因：

- 官方示例直接使用 `waveshare/esp32_s3_touch_amoled_2_06` BSP。
- 官方 BSP 管理屏幕、电源、触摸等硬件初始化，稳定性高于本地手改硬件层。
- 官方 Brookesia Phone UI 已有 App 菜单、状态栏、导航栏等基础结构。
- 后续只在应用层增加表盘首页、OTA App、设置 App、中文字体补字。

## 分区布局

官方原始示例是单 `factory` app 分区，不适合当前手表 OTA 和应用商店需求。

本工程已改成兼容旧系统的数据布局：

```csv
nvs,            data, nvs,     0x9000,   0x6000,
otadata,        data, ota,     0xF000,   0x2000,
phy_init,       data, phy,     0x11000,  0x1000,
ota_0,          app,  ota_0,   0x20000,  0xB00000,
ota_1,          app,  ota_1,   0xB20000, 0xB00000,
littlefs_data,  data, littlefs,0x1620000,0x900000,
```

意义：

- `nvs`：保留 WiFi 记录、系统设置。
- `otadata`：保留 OTA 启动槽状态。
- `ota_0` / `ota_1`：双 OTA 固件分区。
- `littlefs_data`：保留已下载 App 和资源文件。

## 安全刷写原则

没有明确确认前，不刷：

- `nvs`
- `littlefs_data`
- `otadata`
- `bootloader`

验证新固件时优先只刷：

- `0x20000` ota_0 app
- `0xB20000` ota_1 app

示例：

```bash
/home/bo/.espressif/python_env/idf6.0_py3.13_env/bin/python -m esptool \
  --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 32MB --flash-freq 80m \
  0x20000 build/esp-brookesia.bin \
  0xB20000 build/esp-brookesia.bin
```

只有在分区表本身发生变化、并且确认需要重建数据布局时，才刷 `0x8000`
partition table。日常应用层升级不要刷 `nvs` / `littlefs_data` / `otadata`，
这样可以保留 WiFi 记录、设置项、AppStore 下载应用和资源文件。

## 构建

```bash
cd /home/bo/esp_brookesia_watch/official_watch_os
source /home/bo/esp_brookesia_watch/esp-idf/export.sh
idf.py build
```

当前构建结果：

- App：`build/esp-brookesia.bin`
- 版本：`0.1.58`
- 大小：约 3.9 MB，二进制大小 `0x3de9b0`
- SHA256：`9a1db63f382030b7a2fcfdc5d7d86469a1f3bc153bd652dc99676c75fa3bedfa`
- OTA app 分区：11 MB
- 余量：约 65%

## 屏幕显示注意点

这块屏幕是矩形圆角 AMOLED，不是圆形屏。顶部左右圆角会裁掉贴边内容，
不能用黑色遮盖白边来规避初始化问题，也不能整体下移牺牲顶部区域。

当前原则：

- LCD 初始化必须按 Waveshare 官方 BSP/示例配置走。
- UI 顶部状态栏和表盘内容使用圆角安全区。
- 左右状态信息不要贴到极限边缘，避免被圆角裁切。
- 首页顶部只放 WiFi/电量等轻量状态；主要时间、日期放在可视安全区内。

## 当前已集成应用/功能

基于官方 Brookesia Phone UI，不引入第二套系统：

- Watch Home：开机直接进入表盘首页，底部菜单按钮返回 Brookesia App 菜单。
- Settings：WLAN、二维码配网入口、忘记网络、重新连接/自动切换、附近 WiFi 扫描、已保存 WiFi、亮度、自动息屏时间、抬腕亮屏、时间/RTC/SNTP、电池/电源、传感器、存储、音频、关于。
- Quick：轻量快捷控制中心，只保留 WiFi 摘要、电量摘要、亮度滑条、音量滑条和刷新/返回；完整设置放在 Settings。
- OTA：云端版本检查、确认更新、下载/写入进度、固件头校验、版本校验、切换 OTA 分区并重启。
- Alarm：本地闹钟 App，支持小时/分钟设置、启用/关闭、每日重复/单次、停止响铃；配置保存在 NVS，OTA 后保留。
- Calendar：基于 LVGL 官方 `lv_calendar` 控件的轻量日历 App，支持显示当前月份、左右切换月份、回到今天、选择日期和高亮今天。
- Anniv：纪念日 App，支持 4 个纪念日槽位、D-/D+/Today、每年重复/单次，配置保存在 NVS。
- Weather：天气 App，当前显示 WiFi/时间状态和天气源占位；后续接入云端天气接口时不应在 UI 线程同步联网。
- Timer：秒表/倒计时 App，支持开始/暂停、重置、模式切换、倒计时 +/-1 分钟和到点提示音。
- Cloud：云服务器状态监控 App，读取 DUDUSERVER 设备状态、天气、设备配置摘要。
- Quota：API 额度监控 App，读取 DUDUSERVER `api-usage-status` 摘要；本机 ccswitch 同步只上传脱敏后的余额/用量字段。
  - 当 ccswitch 只暴露 live balance 时，Quota App 显示 `Balance`，不伪造月限额/剩余额度。
- Battery/Power：接入 AXP2101，显示实际百分比、电压、VBUS、VSYS、电源来源、充电状态等。
- Sensors：读取 QMI8658 IMU 原始 ACC/GYRO 数据。
- Files：浏览 LittleFS 数据分区，并显示 SD/TF 状态。LittleFS 挂载失败不会格式化，保护已下载应用。
- Storage：Settings 中同时显示 LittleFS 数据分区和 SD/TF 状态。
- Audio Lite：音量调节、音频服务状态、测试音入口。
- Music：基于 Waveshare 官方 `bsp_extra` / `esp-audio-player` 播放链路，UI 复用官方 SquareLine 音乐页风格；支持从 TF 卡 `/sdcard/music` 或 `/sdcard` 扫描 `.mp3/.wav`，并内置 LittleFS 测试音乐和测试音。
- Mic Spectrum：基于 Waveshare 官方 `05_Spec_Analyzer` 示例，接入 ES7210 麦克风输入和 `esp-dsp` FFT，显示实时麦克风频谱。
- 状态栏：显示 WiFi 连接状态和真实电量百分比。
- Time / RTC：Settings 中可查看系统时间、RTC 时间、SNTP 状态，并手动触发 SNTP 或从 RTC 恢复系统时间。
- About：显示固件版本、项目名、运行/下一 OTA 分区、堆内存、PSRAM、运行时长、MAC。
- Display：可调亮度、自动息屏时间、抬腕亮屏开关。
- PWR Home：单击 PWR 从菜单/应用返回 WatchHome；按键空闲电平自动识别，保留长按电源行为。
- App Icons：菜单图标已从少量 quick settings 图标复用，改为每个主要 App 使用不同的 48×48 线框图标。

## Mic Spectrum / 麦克风频谱

来源：

- Waveshare 官方示例：`examples/esp-idf/05_Spec_Analyzer`
- BSP 接口：`bsp_audio_codec_microphone_init()`
- DSP 依赖：`espressif/esp-dsp` `1.7.0`

当前实现：

- 新增 `Mic Spectrum` App，保持 Brookesia App 风格和 Launcher 注册方式。
- 音频硬件访问封装在 `components/watch_audio`，App 不直接散落调用 BSP 细节。
- 使用 ES7210 麦克风，16 kHz、16 bit、双声道读取。
- 使用 `dsps_fft2r_fc32` + Hann window 做 1024 点 FFT。
- UI 用 40 条彩色柱状条显示实时频谱，关闭 App 时释放 LVGL timer 并请求停止麦克风任务。
- 当前没有加入录音保存和音频播放文件功能；这两个功能依赖 SD/文件格式/解码链路，后续单独做。
- 音乐文件播放已在 `Music` App 中单独接入；频谱 App 启动麦克风前会停止音乐播放，避免 I2S/Codec 冲突。

兼容点：

- `esp-dsp 1.7.0` 的 Kalman C++ 源在当前 ESP-IDF 6 / gnu++26 / picolibc 组合下会出现 `std::sin/cos/atan` 未声明。
- 项目通过 `main/dsp_cxx_math_compat.hpp` 对 `esp-dsp` C++ 源做预包含兼容，不直接修改 `managed_components` 第三方源码。

## Music / 音乐播放器

来源：

- Waveshare 官方示例：`examples/esp-idf/05_Spec_Analyzer/components/bsp_extra`
- Waveshare 官方示例：`examples/esp-idf/06_videoplayer/components/bsp_extra`
- 播放器依赖：`chmorgan/esp-audio-player` `1.1.0`
- MP3 解码依赖：`chmorgan/esp-libhelix-mp3` `1.0.3`

当前实现：

- 新增 `Music` App，使用 Brookesia App 注册方式出现在官方菜单中。
- UI 不再使用简陋列表页，改为接近 Waveshare 官方 SquareLine demo 的白色纹理背景、专辑图、圆形播放键、上一首/下一首布局。
- `components/watch_audio` 封装播放器初始化、I2S 写入、采样率切换、播放/暂停/恢复/停止和文件扫描。
- 支持 `.mp3` / `.wav`。
- 优先扫描 `/sdcard/music`，没有音乐时回退扫描 `/sdcard`。
- 无 TF 卡或 TF 卡无音乐时，会确保并扫描：
  - `/littlefs/music/test-music.wav`：6 秒、16 kHz、16 bit、mono WAV 小旋律。0.1.35 起内置 `Test Music` 播放改为后台直接合成并写 codec，避免文件播放器链路闪退。
  - `/littlefs/music/test-tone.wav`：1 秒、8 kHz、16 bit、mono WAV 测试音；播放时走后台硬件直写链路，避免阻塞 UI。
- 内置测试文件只在不存在或尺寸异常时写入，不格式化、不清空 LittleFS，不影响 App Store 下载内容。
- 播放音乐前会停止麦克风；麦克风频谱启动前也会停止音乐，避免音频硬件冲突。
- 音乐 App 关闭不会强制停止播放，用户需要点 `Stop` 停止。

使用方式：

1. 无 TF 卡时：打开菜单里的 `Music` App，点 `Play` 播放内置 `Test Music`，用于测试真实 WAV 文件播放链路。
2. 有 TF 卡时：在 TF 卡创建目录 `/music`。
3. 放入 `.mp3` 或 `.wav` 文件。
4. 插入手表 TF 卡。
5. 打开菜单里的 `Music` App，点 `Scan` / `Play`。

## OTA 云端协议

当前 OTA App 使用项目现有服务器：

- `POST http://<OTA_SERVER>/DUDUSERVER/api/v1/auth/login`
- `GET  http://<OTA_SERVER>/DUDUSERVER/api/v1/device-config`
- `PUT  http://<OTA_SERVER>/DUDUSERVER/api/v1/device-status`
- 固件下载只支持 `http://`。

OTA 判断条件：

- `ota_enabled == true`
- 远端 `firmware_version` 高于本机版本
- `firmware_url` 非空且是 `http://`
- 固件大小小于当前 OTA 目标分区
- 固件头 magic 正确
- 固件内 `esp_app_desc.version` 等于云端 `firmware_version`

OTA 写入策略：

- 下载数据直接写入 `esp_ota_get_next_update_partition(nullptr)` 返回的另一个 app 分区。
- 写入完成后执行 `esp_ota_end()` 校验。
- 校验成功后 `esp_ota_set_boot_partition()`，再 `esp_restart()`。
- OTA 不写 NVS/LittleFS，因此保留 WiFi 记录和已下载应用。

## 2026-08-11 0.1.44 ccswitch quota sync

## 2026-08-12 0.1.58 settings health / OTA diagnostics

- Settings 新增 `Health` 系统健康诊断页：
  - 固件版本；
  - running / next OTA partition；
  - reset reason；
  - uptime；
  - WiFi status；
  - power status；
  - display status；
  - heap free / largest；
  - PSRAM free / largest；
  - task count。
- OTA App 状态上报增强：
  - `running_partition`；
  - `next_partition`；
  - `heap_largest_kb`；
  - `psram_free_kb`；
  - `psram_largest_kb`；
  - `reset_reason`。
- OTA 页面增加 detail 行，显示当前阶段：登录、读取配置、打开固件 URL、下载字节进度、版本校验、`esp_ota_end()` 校验、切换启动分区、重启。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。
- 固件版本：`0.1.58`。

## 2026-08-11 0.1.44 ccswitch quota sync

- ccswitch API 用量同步默认读取本机 `~/.cc-switch/cc-switch.db`：
  - 当前 provider 来自 `providers.is_current`。
  - 月用量来自 `usage_daily_rollups.total_cost_usd`。
  - 月额度来自 `providers.limit_monthly_usd`。
  - 不读取、不上传 `settings_config` 中的 API key/token。
- 如果 ccswitch 没有配置月额度，Quota App 显示真实已用金额和 `limit not set`，不显示假剩余额度。
- 固件版本：`0.1.44`。

## 2026-08-11 0.1.43 no-fake-quota update

- Quota App 不再把 `not_synced` 显示为 `0% Used` 或 `0` 余额。
- DUDUSERVER quota 同步接口拒绝空/默认零值；没有真实采集数据时保持 `not_synced`。
- 固件版本：`0.1.43`。

## 2026-08-10 快照

本次状态：

- 已构建通过。
- 当前 0.1.29 已构建通过，并生成 release：
- 当前 0.1.30 已构建通过，并生成 release：
  `releases/0.1.30-watch-os-brookesia.bin`
- 当前 0.1.31 已构建通过，并生成 release：
  `releases/0.1.31-watch-os-brookesia.bin`
- 0.1.31 将 `Music` App UI 改为官方 SquareLine demo 风格，并加入 LittleFS 内置测试 WAV。
- 当前 0.1.32 已构建通过，并生成 release：
  `releases/0.1.32-watch-os-brookesia.bin`
- 0.1.32 修复 Music App 首次播放时可能关闭未打开 speaker codec 导致重启的问题；不修改 NVS/LittleFS/otadata。
- 当前 0.1.33 已构建通过，并生成 release：
  `releases/0.1.33-watch-os-brookesia.bin`
- 0.1.33 将内置 `Test Tone` 改为官方 BSP 直接硬件播放链路，并保护播放器 mute 回调不访问未打开 codec；TF 卡音乐仍保留文件播放器。
- 当前 0.1.34 已构建通过，并生成 release：
  `releases/0.1.34-watch-os-brookesia.bin`
- 0.1.34 修复内置测试音阻塞 UI 刷新导致花屏/按行刷新的问题，并新增真正走文件播放器链路的内置 `Test Music` WAV 小旋律。
- 0.1.34 放宽抬腕亮屏阈值，加入 Settings 显示状态诊断：IMU 状态、motion 数值和最近抬腕判定原因。
- 当前 0.1.35 已构建通过，并生成 release：
  `releases/0.1.35-watch-os-brookesia.bin`
- 0.1.35 修复 OTA 页面卡屏：后台 OTA task 只更新内存状态，LVGL timer 统一刷新 UI。
- 0.1.35 修复内置音乐播放闪退：默认 `Test Music` 不再走 `esp-audio-player` 文件解码器，改为后台直接合成旋律写 codec；TF 卡文件播放链路保留。
- 0.1.35 全局卡顿修复：抬腕 IMU 采样移出 50ms UI timer；Sensors 页面使用后台 IMU 采样；Settings WiFi Scan 使用后台扫描。
- 当前 0.1.36 已构建通过，并生成 release：
  `releases/0.1.36-watch-os-brookesia.bin`
- 0.1.36 继续修复全局卡顿：PWR AXP2101 IRQ 轮询移出 50ms LVGL timer；电量/充电状态改为后台 AXP2101 缓存，UI 线程只读缓存。
- 当前 0.1.37 已构建通过，并生成 release：
  `releases/0.1.37-watch-os-brookesia.bin`
- 0.1.37 修复 Settings 点击配网卡屏：WiFi APSTA/SoftAP/HTTP/DNS 启动移到后台 task，UI 线程只显示启动状态并通过 timer 刷新。
- 当前 0.1.38 已构建通过，并生成 release：
  `releases/0.1.38-watch-os-brookesia.bin`
- 0.1.38 新增 `Alarm` 本地闹钟 App 和 `watch_alarm` 服务：配置保存在 NVS，OTA 后保留；到点后唤醒屏幕并打开闹钟页，响铃通过后台音频任务触发。
- 当前 0.1.39 已构建通过，并生成 release：
  `releases/0.1.39-watch-os-brookesia.bin`
- 0.1.39 新增 `Calendar` 日历 App：基于 LVGL 官方日历控件，支持月份切换、回到今天、选择日期和高亮今天；不写 NVS/LittleFS。
- 当前 0.1.40 已构建通过，并生成 release：
  `releases/0.1.40-watch-os-brookesia.bin`
- 0.1.40 优化菜单图标：新增运行时线框图标，Home、Files、OTA、Sensors、Spectrum、Quick、Audio、Music、Alarm、Calendar 不再复用同一批 quick settings 图标。
- 当前 0.1.41 已构建通过，并生成 release：
  `releases/0.1.41-watch-os-brookesia.bin`
- 0.1.41 新增 `Anniv` 纪念日 App、`Weather` 天气 App、`Timer` 计时 App，并给 Anniv/Weather/Timer 增加独立菜单图标；未刷 NVS、LittleFS、otadata、bootloader、partition table。
- 0.1.41 已上传并推送 OTA：固件 ID `122`，URL `http://<OTA_SERVER>/DUDUSERVER/firmware/0.1.41-2ac064b097ee42ccb04daf9603c76bac.bin`，远端 bin 与本地 release SHA256 一致。
- 当前 0.1.42 已构建通过，并生成 release：
  `releases/0.1.42-watch-os-brookesia.bin`
- 0.1.42 新增 `Cloud` 云服务器状态监控 App 和 `Quota` API 额度监控 App；两个 App 的 HTTP 请求都在后台 task 中执行，UI 线程只刷新缓存。
- 0.1.42 已上传并推送 OTA：固件 ID `123`，URL `http://<OTA_SERVER>/DUDUSERVER/firmware/0.1.42-bde44a478eb840e58be45fc1d3a01a90.bin`，远端 bin 与本地 release SHA256 一致。
- 未刷 NVS、LittleFS、otadata、bootloader、partition table。
