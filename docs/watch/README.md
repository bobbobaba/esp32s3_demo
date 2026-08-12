# ESP32-S3 Touch AMOLED 2.06 Watch OS

这是给 Waveshare `ESP32-S3-Touch-AMOLED-2.06` 手表板适配的 ESP-Brookesia 系统工程。

当前固件基于 ESP-Brookesia `System Super` 示例，已适配 410 × 502 AMOLED、FT3168 触摸、LittleFS 资源分区、Settings / App Store / Files / OTA 更新内置应用，并修复了屏幕顶部白边和圆角裁切问题。

## 硬件

- 开发板：Waveshare ESP32-S3-Touch-AMOLED-2.06
- MCU：ESP32-S3，8 MB PSRAM，32 MB Flash
- 屏幕：2.06 英寸 AMOLED，410 × 502，QSPI
- 显示驱动：`SH8601`
- 触摸：FT3168，使用 ESP-IDF `esp_lcd_touch_ft5x06` 驱动
- 电源管理：AXP2101
- 常见串口：`/dev/ttyACM0`

官方资料：

- 文档：https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-2.06
- 官方示例仓库：https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
- 官方 BSP / 组件仓库：https://github.com/waveshareteam/Waveshare-ESP32-components

## 目录结构

```text
esp_brookesia_watch/
├── esp-idf/          # ESP-IDF 6.0 环境
├── esp-brookesia/    # ESP-Brookesia 源码和板级适配
├── watch_os/         # 旧工程/早期验证工程
└── official_watch_os/# 当前推荐工程：基于 Waveshare 官方 Brookesia 示例继续二次开发
```

当前实际开发入口：

- 工程：`/home/bo/esp_brookesia_watch/official_watch_os`
- 构建输出：`official_watch_os/build/esp-brookesia.bin`
- 最新 release：`releases/0.1.60-watch-os-brookesia.bin`
- 最新 SHA256：`487d2c2f2931a55f28f28cd04a6bfede1f2745b046fa0999d882c4bf6599678f`
- 最新 OTA 固件 ID：`138`
- 最新 OTA URL：`<PRIVATE_OTA_FIRMWARE_URL>`

0.1.60 新增：

- Settings 菜单第一项改为 `System Health`，解决手表端 Settings 页面里不容易看到 Health 页的问题。
- 状态栏 WiFi 图标刷新周期从 1 秒改为 5 秒。
- Weather App 复用 `watch_weather` 共享天气服务，和首页共用同一份天气缓存，避免两套 HTTP/JSON 逻辑。
- 清理 Settings 不可达旧 UI 代码。
- 首页主时间显示放大约 17%。
- 只修入口可见性，不改 NVS / LittleFS / SD / otadata。

0.1.59 新增：

- Settings `Health` 页继续增强：显示 WiFi 诊断、Storage 缓存诊断、Audio 状态、OTA 目标分区容量、heap / PSRAM / task 状态。
- 新增 `watch_connectivity::wifi_diag_text()`：显示 WiFi state、连接状态、配网状态、SSID、RSSI、保存网络数量、最近失败原因、disconnect reason、连接耗时、autoconnect/manual 状态、内部 heap/largest。
- 新增 `watch_storage::storage_diag_text()`：只读缓存诊断，不主动挂载 SD/LittleFS，避免 Health 页刷新时引入存储阻塞。
- WiFi 扫描 / 配网 task 创建失败时，页面直接显示 internal heap 和 largest block，便于定位 `no memory`。
- OTA task 创建失败时，detail 行显示 internal heap、largest block、PSRAM 余量。
- 本版本不改显示驱动、不改亮度/息屏策略、不启用 light sleep、不刷 NVS/LittleFS/SD/otadata，继续保留 WiFi 记录和已下载 App。
- 已上传并推送 OTA：固件 ID `137`，远端 bin 与本地 release SHA256 一致。

0.1.58 新增：

- Settings 新增 `Health` 系统健康诊断页，显示固件版本、运行/下一 OTA 分区、重启原因、运行时长、WiFi/电源/显示状态、heap/PSRAM 余量和任务数量。
- OTA App 增强可观测性：状态上报增加运行分区、下一 OTA 分区、largest heap、PSRAM 余量、reset reason；页面增加详细阶段提示，便于定位下载、校验、切分区失败。
- 保持低风险更新：不改亮度和息屏策略，不启用 light sleep，不刷 NVS / LittleFS / SD / otadata，继续保留 WiFi 记录和已下载 App。
- 已本地构建通过：app 分区余量约 65%；已生成 release，待上传 private OTA server 后可 OTA。

0.1.43–0.1.57 历史补录：

- 0.1.43：Quota 不再显示假数据；没有真实 ccswitch/API 用量同步时保持 `not_synced`。
- 0.1.44：接入本机 ccswitch 真实 quota 同步摘要，继续修复 Settings / Storage 稳定性。
- 0.1.45 / 0.1.46：中间验证构建，本地保留 release，未找到独立功能改动记录。
- 0.1.47 / 0.1.48：当前 watch release 目录未保留正式固件，不补写功能，避免混入其他 ESP32 项目记录。
- 0.1.49：Music / `watch_audio` 播放稳定性整理，Quota App 状态字段扩展。
- 0.1.50：纪念日服务增强，为首页选择显示纪念日做准备。
- 0.1.51：抽出 `watch_quota` 服务，供 Quota App 和首页共享真实 API 额度状态。
- 0.1.52：调整构建配置和 WatchHome 组件注册。
- 0.1.53：Settings WiFi 配网/连接服务继续接入 `watch_connectivity`。
- 0.1.54：修复首页 WiFi/天气/圆角安全区相关显示。
- 0.1.55：中间验证构建，本地保留 release，未找到独立功能改动记录。
- 0.1.56：多页面后台化/非阻塞刷新整理，降低 Settings、OTA、天气、配网等页面卡屏概率。
- 0.1.57：后台任务栈和内存压力优化，降低 WiFi 扫描/配网 `no memory` 概率；不改显示驱动、亮度、息屏、NVS/LittleFS/SD 数据。
- 详细逐版本记录见 `official_watch_os/README_WATCH_BASELINE.md` 的“0.1.43 到 0.1.58 更新补录”章节。

0.1.42 新增：

- 新增 `Cloud` 云服务器状态监控 App：后台登录 private OTA server，读取设备在线状态、固件/OTA、内存/RSSI、天气摘要和云端目标版本。
- 新增 `Quota` API 额度监控 App：后台读取 private OTA server `GET /api/v1/api-usage-status`，显示 provider、套餐、额度、已用比例、剩余额度、更新时间和备注。
- 新增 Cloud / Quota 独立 48×48 菜单图标。
- 网络请求全部在后台 FreeRTOS task 中执行，LVGL/UI 线程只读缓存刷新，避免卡屏。
- Quota 不把 ccswitch/API key 烧进手表；本机 ccswitch 用量通过服务器接口同步脱敏摘要，手表只读云端摘要。
- 本版本未刷 NVS、LittleFS、otadata、bootloader、partition table。
- 已上传并推送 OTA：固件 ID `123`，远端 bin 与本地 release SHA256 一致。

0.1.41 新增：

- 新增 `Anniv` 纪念日 App：支持 4 个纪念日槽位、日期调整、启用/关闭、每年重复/单次、D-/D+/Today 显示；配置保存到 NVS，OTA 后保留。
- 新增 `Weather` 天气 App：先提供 Brookesia App 风格页面、WiFi/时间状态和天气源占位；当前不在 UI 线程联网，后续可接入云端天气接口。
- 新增 `Timer` 计时 App：支持秒表/倒计时模式、开始/暂停、重置、倒计时 +/-1 分钟；到点后使用后台测试音提示。
- 菜单图标继续优化，新增 Anniv / Weather / Timer 独立 48×48 线框图标。
- 保留 0.1.40 的菜单图标优化；本版本未刷 NVS、LittleFS、otadata、bootloader、partition table。
- 已上传并推送 OTA：固件 ID `122`，远端 bin 与本地 release SHA256 一致。

0.1.40 新增：

- 优化菜单图标，避免多个 App 复用同一 quick settings 图标。
- 新增 `watch_app_icons` 运行时线框图标：Home、Files、OTA、Sensors、Spectrum、Quick、Audio、Music、Alarm、Calendar。
- Settings 继续使用官方齿轮图标，SquareLine Demo 继续使用官方 demo launcher 图标。
- 图标不写 LittleFS，不引入 PNG 资源包，OTA 只更新 app 分区。

0.1.39 新增：

- 新增 `Calendar` 日历 App，基于 LVGL 官方 `lv_calendar` 控件。
- 支持显示当前月份、左右切换月份、回到今天、选择日期。
- 高亮今天；系统时间未同步时使用备用日期并在页面提示 `Time not synced`。
- 日历 App 不做网络、文件、I2C 操作，不写 NVS/LittleFS，避免引入新的卡屏点。

0.1.38 新增：

- 新增 `Alarm` 闹钟 App：支持设置小时/分钟、启用/关闭、每日重复/单次、停止响铃。
- 新增 `watch_alarm` 本地服务：闹钟配置保存到 NVS，OTA 后保留；不写 LittleFS，不影响 App Store 下载应用。
- 到点后自动唤醒屏幕并打开 `Alarm` 页面；闹钟检测只读取系统时间，不在检测逻辑中做网络、文件或 I2C 操作。
- 响铃使用后台测试音任务触发，避免在 LVGL/UI 线程同步播放音频。

0.1.37 新增：

- 修复 Settings 点击 `Start WiFi Setup` 配网时卡屏：APSTA 切换、SoftAP 配置、HTTP 配网页、DNS captive portal 启动全部移到后台 task。
- Settings UI 点击配网后立即显示 `Starting WiFi setup...`，后台完成后再刷新 AP/二维码信息。

0.1.36 新增：

- 继续修复全局卡屏：PWR 返回首页的 AXP2101 IRQ 轮询从 50ms LVGL timer 移到后台 task，UI 线程只消费按键 pending 标志。
- 电量/充电状态改为后台 AXP2101 缓存：状态栏、首页、Quick、Settings 不再直接在 UI 线程读电源 I2C。

0.1.35 新增：

- 修复 OTA 页面下载/写入时卡屏：后台 OTA task 不再直接频繁操作 LVGL 控件，只更新内存状态；页面通过 250ms LVGL timer 统一刷新。
- 修复内置音乐点击播放闪退：`Test Music` 暂时改为后台合成 6 秒小旋律并直接写官方 codec，不再走不稳定的 `esp-audio-player` 文件解码器。
- `Stop` 对内置测试音乐生效；TF 卡 `.mp3/.wav` 文件播放器链路继续保留，后续需要串口日志再单独修。
- 全局卡顿修复：抬腕 IMU 采样从 50ms UI timer 移到后台 task，UI timer 只处理触摸、息屏和后台唤醒标志。
- Sensors 页面 IMU 读取改为后台采样 + UI 缓存刷新，避免传感器页每秒阻塞 UI。
- Settings 的 WiFi Scan 改为后台扫描 + timer 刷新结果，避免按钮点击后页面卡住。

0.1.34 新增：

- 修复 Music / Audio Lite 播放内置 `Test Tone` 时阻塞 LVGL/UI 线程的问题：测试音改为后台 FreeRTOS task 播放，避免 AMOLED 出现花屏、一行一卡一行刷新的现象。
- `Music` App 新增内置音乐样本 `/littlefs/music/test-music.wav`：6 秒、16 kHz、16 bit、mono WAV，小旋律文件。0.1.35 起默认播放内置旋律时改走后台直写 codec，避免文件播放器闪退。
- 无 TF 卡时默认选中 `Test Music`，下一首仍保留 `Test Tone`。
- 放宽抬腕亮屏识别阈值：从“强运动后稳定 5 次”改为“翻腕/抬腕候选后稳定 2 次确认”，同时保留跑步大幅抖动屏蔽。
- Settings 的显示状态增加 IMU 读取状态、最近 motion 数值和抬腕判定原因，便于继续定位。

0.1.33 新增：

- 内置 `Test Tone` 改为走 Waveshare BSP 官方文档推荐的直接 `esp_codec_dev_open/write/close` 硬件播放链路，不再经过 `esp-audio-player` 文件解码器，先验证音频硬件稳定性。
- `audio_player` 的 mute 回调在 speaker codec 未 open 时直接返回成功，避免初始化阶段访问未打开 codec。
- TF 卡 `.mp3/.wav` 文件播放仍保留 `esp-audio-player` 链路。

0.1.32 新增：

- 修复 Music App 点击 Play 后重启：`watch_audio` 维护 speaker codec open 状态，只在已 open 时调用 `esp_codec_dev_close()`，避免首次播放关闭未打开 codec。
- 保留 0.1.31 的官方 SquareLine demo 风格播放器 UI 和 LittleFS 内置测试 WAV。

0.1.31 新增：

- `Music` App：UI 改为接近 Waveshare 官方 SquareLine demo 的白色纹理背景、专辑图、圆形播放键、上一首/下一首风格。
- `Music` App：启动/扫描时会在 LittleFS 自动生成一个很小的标准 WAV 测试音 `/littlefs/music/test-tone.wav`，无 TF 卡也能测试音频硬件。
- `Music` App：仍优先扫描 TF 卡 `/sdcard/music` 和 `/sdcard` 中的 `.mp3/.wav`，没有 TF 卡时回退到内置测试音。
- `watch_audio` 新增音乐扫描、播放、暂停、恢复、停止封装，并处理播放器和麦克风频谱之间的 Codec/I2S 互斥。
- 继续保留 0.1.29 的 `Mic Spectrum` App。

关键文件：

- `official_watch_os/main/main.cpp`：启动 Brookesia Phone UI，默认进入 WatchHome。
- `official_watch_os/partitions.csv`：32 MB Flash 双 OTA + LittleFS 分区表。
- `official_watch_os/README_WATCH_BASELINE.md`：当前官方基线工程说明。
- `esp-brookesia/hal/brookesia_hal_boards/boards/waveshare/esp32_s3_touch_amoled_2_06/board_devices.yaml`：板级显示和触摸配置。
- `esp-brookesia/hal/brookesia_hal_boards/boards/waveshare/esp32_s3_touch_amoled_2_06/setup_device.c`：LCD / Touch 工厂函数和官方 LCD 初始化表。
- `esp-brookesia/hal/brookesia_hal_boards/boards/waveshare/esp32_s3_touch_amoled_2_06/power_manager.c`：AXP2101 电池状态读取和充电配置接口。
- `esp-brookesia/system/brookesia_system_super/resource/...`：Brookesia Shell 源资源。
- `watch_os/littlefs/...`：构建时生成的暂存资源，不要手动改这里；会被构建流程覆盖。

## 当前已修复的问题：顶部白边

问题现象：系统启动后屏幕上边一直有白边，改 UI 暂存资源或单独改偏移没有效果。后续确认：不能用黑色状态栏、黑色矩形或整体下移 UI 来“挡住”白边；这样会裁掉顶部可显示区域，导致电量、Wi-Fi 等顶部内容只显示一半。

最终有效修复：

1. 显示驱动按 Waveshare 官方 BSP 切到 `waveshare/esp_lcd_sh8601`。
2. LCD 初始化表使用官方 BSP 的 `SH8601` 初始化序列，包含 `0x2A` / `0x2B` 显示窗口设置。
3. 当前窗口参数：
   - `0x2A {0x00, 0x16, 0x01, 0xAF}`
   - `0x2B {0x00, 0x00, 0x01, 0xF5}`
4. 保留 `esp_lcd_panel_set_gap(panel, 0x16, 0)`，让 LVGL 坐标映射到真实屏幕窗口。
5. LCD 初始化后调用 `lcd_panel_clear_black()` 对完整 `410 × 502` 显示区做一次真实清屏，清掉控制器未刷新的残留白区。
6. Brookesia overlay 状态栏默认隐藏，`shell.statusBar` 设为透明，避免系统状态栏叠在首页顶部。

注意：不要再用 `CO5300` 方案修这个板子的白边。虽然部分资料会提到 CO5300，但 Waveshare 当前官方 ESP-IDF BSP `esp32_s3_touch_amoled_2_06` 使用的是 `SH8601` 组件。

关键文件：

- `esp-brookesia/hal/brookesia_hal_boards/boards/waveshare/esp32_s3_touch_amoled_2_06/setup_device.c`
- `esp-brookesia/system/brookesia_system_super/resource/shell/screens/overlay.json`
- `esp-brookesia/system/brookesia_system_super/resource/shell/styles/shell.json`

## 圆角安全区

这块 2.06 英寸屏幕是圆角面板。修复原则：

- 不整体下移首页，不浪费顶部中间可显示区域。
- 只把顶部状态行做局部安全区内收，避开左上和右上圆角裁切。
- 首页 `top_status` 当前配置为：
  - `x = 52dp`
  - `y = 24dp`
  - `width = env.widthDp - 104dp`
  - `height = 44dp`
- Wi-Fi / 电量胶囊放在 `top_status` 内，尺寸约为原小版本两倍，同时避免右上角圆角切掉文字和背景。

关键文件：

- `esp-brookesia/system/brookesia_system_super/resource/shell/screens/home.json`

## 当前首页功能状态

- 首页时间 / 日期：已绑定系统时间，Shell 每秒刷新一次。
- 时间同步：System Super 启动时会尝试启动 SNTP 服务；连上 Wi-Fi 后系统时间可同步。
- Wi-Fi 状态：首页右上角已绑定 Wi-Fi Service 状态，连接时显示 `WiFi`，未连接或未启动时显示 `OFF`。
- 应用入口：首页底部九宫格按钮绑定 `super.nav.open_launcher`，点击进入应用菜单。
- 电量状态：首页右上角已绑定 Device Service + AXP2101 HAL，优先显示真实百分比，例如 `85%`；没有百分比但检测到充电时显示 `CHG`；外部供电但无百分比时显示 `USB`；读取失败或接口不可用时显示 `--`。Shell 每 30 秒刷新一次。

首页菜单按钮修复：

- `home.json` 中底部九宫格按钮仍使用 `super.nav.open_launcher`。
- `shell_display.cpp` 已在 Shell 启动时订阅 `super.nav.open_launcher` 和 `super.nav.home`。
- 点击首页九宫格时走 `System::open_app_launcher()`，确保 Shell 页面状态和系统前后台状态一致。

PWR 返回表盘修复：

- PWR 不是普通 GPIO 轮询，按 Waveshare 官方 `01_AXP2101` 示例走 AXP2101 PKEY IRQ。
- AXP2101 `INTEN2` 同时启用 PKEY press/release edge、short press、long press。
- 单击 PWR 按“松开边沿或 short press”处理：先发送 Brookesia `HOME` 导航事件，让框架暂停当前 App / 关闭 Recents；再延迟启动 `WatchHome`，确保最终回到表盘首页。
- 不能只重新 `START WatchHome`，否则框架当前 active app 状态可能不正确，表现为在菜单或 App 内单击 PWR 没反应。

电量相关配置：

- `board_devices.yaml` 增加 `axp2101_power_manager`，I2C 地址 `0x34`，频率 `400000`。
- `sdkconfig.defaults.board` / `watch_os/sdkconfig` 启用：
  - `CONFIG_ESP_BOARD_DEV_CUSTOM_SUPPORT=y`
  - `CONFIG_BROOKESIA_HAL_ADAPTOR_ENABLE_POWER_DEVICE=y`
  - `CONFIG_BROOKESIA_HAL_ADAPTOR_POWER_ENABLE_BATTERY=y`
  - `CONFIG_BROOKESIA_HAL_ADAPTOR_POWER_BATTERY_IMPL_AXP2101=y`

## 当前内置 App / 基础功能

当前 `watch_os/main/idf_component.yml` 已接入这些 Brookesia App 和服务：

- `Settings`：系统设置页，包含 Wi-Fi 扫描/连接、显示亮度、声音音量、语言、时区、设备信息等页面。
- `App Store`：Brookesia 运行时应用商店框架，依赖 HTTP / Storage / SNTP 等服务。
- `Files`：文件管理器，用于浏览系统可见存储卷。
- `OTA 更新`：本项目新增的手表固件更新 App，App ID 为 `bo.watch.ota`。

注意：

- 当前先复用框架已有 Settings 页面。
- Settings 已有 Wi-Fi 扫描和密码连接；二维码配网不是当前框架现成页面，后续需要在 Settings 或单独 App 中自定义实现。
- 不接入 Waveshare 官方完整示例系统，避免形成两套系统；官方示例只作为硬件初始化和功能参考。

## OTA 更新 App

OTA 更新不是后台静默升级，而是一个可见 App：

1. 从 Launcher 打开 `OTA 更新`。
2. 点击 `检测更新`，设备登录服务器并拉取 OTA 策略。
3. 如果服务器版本高于当前固件版本，页面显示新版本和更新说明。
4. 用户点击 `确认更新` 后才会下载固件、写入另一个 OTA 分区、设置启动分区并重启。
5. 页面显示下载进度和写入进度。

当前 OTA 服务器链路：

- 登录：`POST <PRIVATE_OTA_BASE_URL>/api/v1/auth/login`
- 配置：`GET <PRIVATE_OTA_BASE_URL>/api/v1/device-config`
- 状态：`PUT <PRIVATE_OTA_BASE_URL>/api/v1/device-status`
- 固件下载：`GET <PRIVATE_OTA_BASE_URL>/firmware/<firmware>.bin`

设备端限制：

- 当前只支持 `http://` 固件 URL，不支持 `https://`。
- Wi-Fi 未连接时，OTA App 会提示先进入 Settings 配网。
- 登录账号当前写在 App 代码里：`<PRIVATE_USER>` / `<PRIVATE_PASSWORD>`，后续如果要公开发布固件，需要改成设备绑定或安全 token 方案。
- OTA App 只写 ESP32 OTA app 分区，不写 `nvs`、不写 `littlefs_data`：
  - Wi-Fi 连接记录保留在 `nvs`。
  - App Store 下载的软件和用户文件保留在 `littlefs_data`。
  - 这也是后续正式远程 OTA 的默认策略。
- 开发阶段的 `idf.py flash` 会同时写入 `littlefs_data.bin`，会覆盖 LittleFS 内容；如果要保留 App Store 下载内容，应使用 `idf.py app-flash` 或 OTA App 更新。
- 如果未来需要 OTA 更新系统资源、内置 App 页面或图标，不能直接整包覆盖 LittleFS；需要做“资源包下载 + 合并/迁移”，只覆盖系统资源路径，并保留 `/apps` 下用户安装的运行时应用。

当前 OTA App 行为：

- 点击 App 后不再因初始化 UI binding 失败而让整个 App 启动失败；失败只记录日志。
- 按钮 action 使用 Brookesia 标准 `on_action()` 分发。
- 确认更新前会重新登录/拉取服务器配置，避免使用过期 token 或旧 OTA 策略。
- 下载前会检查固件大小是否超过目标 OTA 分区。
- 下载/写入阶段每 5% 或至少每 10 秒上报一次进度。
- 状态上报包含当前固件版本、目标版本、目标 OTA 分区、OTA 进度、已收字节、总字节、RSSI、运行时长、剩余堆、重启原因、Wi-Fi 状态。

关键文件：

- `esp-brookesia/app/brookesia_app_watch_ota/`
- `watch_os/main/idf_component.yml`

## OTA 校验失败修复记录

问题现象：

- `0.1.11` 固件通过 OTA 下载完整，远端 bin 与本地 bin 的 SHA256 一致。
- `esp_ota_end()` 返回 `ESP_ERR_OTA_VALIDATE_FAILED`，页面提示文件校验失败。
- 后台状态曾显示类似：`OTA失败：官方镜像校验失败：ESP_ERR_OTA_VALIDATE_FAILED，已收 6764336B/6764336B`。

最终根因：

- 旧分区表把 `ota_1` 放在 `0xB20000`，大小 `0xB00000`。
- `0.1.11` 固件大小约 `6764336` 字节，写入 `ota_1` 后结束地址约为 `0x1193730`。
- 该地址跨过 16 MB 边界 `0x1000000`。
- 当前 ESP32-S3 配置没有启用实验性 32-bit flash mmap，ESP-IDF 的镜像校验在 24-bit flash mmap 下不能映射跨 16 MB 的 app 镜像，所以校验失败。

修复方案：

```csv
ota_0,          app,  ota_0,   0x20000,  0x780000,
ota_1,          app,  ota_1,   0x7A0000, 0x780000,
littlefs_data,  data, littlefs,0x1620000,0x900000,
```

- 两个 OTA app 分区都放在 16 MB 以下。
- `littlefs_data` 仍保持 `0x1620000`，不迁移、不覆盖已下载 App。
- `nvs` 仍保持 `0x9000`，Wi-Fi 连接记录保留。
- 不依赖 32-bit mmap 实验配置，降低后续 OTA 风险。

USB 恢复刷写方式：

```bash
/home/bo/.espressif/python_env/idf6.0_py3.13_env/bin/python -m esptool \
  --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB \
  0x8000 /home/bo/esp_brookesia_watch/releases/0.1.12-partition-table.bin \
  0x20000 /home/bo/esp_brookesia_watch/releases/0.1.12-watch-os-brookesia.bin
```

注意：这个恢复方式只刷 `partition-table` 和当前 app，不刷 `nvs`、`otadata`、`littlefs_data`。

已验证：

- `0.1.12` 通过 USB 写入新分区表和 app，Wi-Fi 记录保留，LittleFS 已下载 App 保留。
- `0.1.13` 通过 OTA 从服务器推送成功，启动日志确认 bootloader 从 `ota_1` 的 `0x7A0000` 加载 `0.1.13`。
- OTA 后保留的 App Store 下载 App 包括：`2048`、`calculator`、`flappy_bird`、`music_player`、`nes_emulator`、`weather`。

## 运行时 App 兼容修复：缺失 interactionTemplate

问题现象：

- OTA App 打开时报错：
  `Node references missing interactionTemplate: press.scale`
- App Store 下载的其他 App 也可能出现类似错误。

根因：

- 一些 App 页面资源引用了 `interactionTemplate`，例如 `press.scale`。
- 但旧 LittleFS 中的 App 资源包不一定包含对应模板。
- 原 parser 遇到缺失模板会直接返回错误，导致整个 App 启动失败。

当前修复：

- 在系统 GUI parser 层做兼容：缺失 `interactionTemplate` 时只打印 warning 并跳过该交互动效，不阻止 App 启动。
- 这样可以兼容已经通过 App Store 下载到 LittleFS 的旧 App，不需要覆盖 LittleFS。
- 代价是：缺失模板的控件可能没有按压缩放动画，但页面应能正常打开。

关键文件：

- `esp-brookesia/gui/brookesia_gui_interface/src/parser_node.cpp`

开发注意：

- 后续新写 App 资源时，仍建议在 `package/res/root.json` 中声明模板文件，并提供类似 `templates/press_scale.json` 的资源。
- 但系统不能依赖每个第三方/下载 App 都打包完整模板；parser 容错应长期保留。

## 可恢复快照

当前工程不是标准单一 git 仓库，恢复以 `snapshots/` 目录里的补丁和文件包为准。

当前圆角安全区 + LCD 初始化修复快照：

- `snapshots/watch_round_safe_lcd_init_2026-08-07_files.tar.gz`
- `snapshots/watch_round_safe_lcd_init_2026-08-07_from_watch_home_verified.patch`

当前首页功能版快照：

- `snapshots/watch_home_status_features_2026-08-07_files.tar.gz`
- `snapshots/watch_home_status_features_2026-08-07_from_round_safe.patch`

当前 OTA App 版快照：

- `snapshots/watch_ota_app_2026-08-08_files.tar.gz`

当前 OTA 保留数据快照：

- `snapshots/watch_ota_preserve_data_2026-08-08_files.tar.gz`
- `snapshots/watch_ota_preserve_data_2026-08-08_esp_brookesia.patch`

当前 OTA 分区修复 + 0.1.13 OTA 验证快照：

- `snapshots/watch_ota_partition_fix_0.1.13_2026-08-08_files.tar.gz`

当前 OTA 进度上报 + 中文菜单字库修复快照：

- `snapshots/watch_ota_progress_fonts_0.1.14_2026-08-08_files.tar.gz`

推荐每次做高风险改动前生成文件包：

```bash
cd /home/bo/esp_brookesia_watch
mkdir -p snapshots
tar -czf snapshots/<name>_files.tar.gz README.md esp-brookesia watch_os
```

如果 patch 头部是 `old/...` 和 `current/...`，恢复方式：

```bash
cd /home/bo/esp_brookesia_watch
patch -p1 < snapshots/<name>.patch
```

如果补丁因上下文变化不能直接应用，使用对应的 `*_files.tar.gz` 手动取回关键文件。

## 构建

进入工程：

```bash
cd /home/bo/esp_brookesia_watch/watch_os
source /home/bo/esp_brookesia_watch/esp-idf/export.sh
```

如果改过板级配置，先重新生成 Board Manager 配置：

```bash
idf.py gen-bmgr-config -b esp32_s3_touch_amoled_2_06
```

这个命令可能会把 `sdkconfig` 备份到：

```text
watch_os/components/gen_bmgr_codes/sdkconfig.bmgr_board.old
```

如果 `sdkconfig` 被移走，需要恢复：

```bash
cp components/gen_bmgr_codes/sdkconfig.bmgr_board.old sdkconfig
```

构建：

```bash
idf.py reconfigure
idf.py build
```

成功时会看到类似：

```text
watch_os.bin binary size ...
Project build complete.
```

## 刷机

先确认串口：

```bash
pio device list
```

正常是：

```text
/dev/ttyACM0
USB VID:PID=303A:1001
```

刷入：

```bash
idf.py -p /dev/ttyACM0 flash
```

正常情况下不需要按 BOOT。BOOT 只在无法自动进入下载模式时使用。

刷完成功会看到：

```text
Hash of data verified.
Hard resetting via RTS pin...
Done
```

## 查看启动日志

```bash
idf.py -p /dev/ttyACM0 monitor
```

退出 monitor：

```text
Ctrl + ]
```

关键日志：

```text
DEV_DISPLAY_LCD: Initializing LCD display: display_lcd, chip: sh8601
sh8601: LCD panel create success, version: 2.0.0
SvcDisplay: Registered display output Output0: 410x502
SysSuper: Super shell started
SysCore: System core started
```

如果看到 `chip: co5300`，说明板级配置没有使用当前正确适配。

## 修改 UI 资源的正确位置

不要改：

```text
watch_os/littlefs/system/super/...
```

这里是构建阶段 staging 出来的结果，下次构建会被覆盖。

应该改：

```text
esp-brookesia/system/brookesia_system_super/resource/...
```

例如：

- `resource/themes/light.json`
- `resource/shell/styles/shell.json`
- `resource/shell/screens/*.json`
- `resource/shell/flows/*.json`

改完后重新执行：

```bash
cd /home/bo/esp_brookesia_watch/watch_os
source /home/bo/esp_brookesia_watch/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash
```

## 常见问题

### 刷完屏幕不亮

先不要反复改代码。按顺序检查：

1. 串口日志是否正常启动。
2. 日志是否显示 `chip: sh8601`。
3. 是否写入了 `littlefs_data.bin`。
4. 设备是否处于 BOOT 下载模式；断电重插或长按 BOOT 释放后再看。

### 上边又出现白边

重点检查：

1. `board_devices.yaml` 是否是 `chip: sh8601`。
2. `setup_device.c` 是否包含官方 `0x2A` / `0x2B` 窗口初始化。
3. `setup_device.c` 是否仍调用 `esp_lcd_panel_set_gap(panel, 0x16, 0)`。
4. `setup_device.c` 是否仍在点亮屏幕前对完整 `410 × 502` 区域执行 `lcd_panel_clear_black()`。
5. `shell.statusBar` 是否保持透明，overlay 状态栏是否默认隐藏。

### Wi-Fi 怎么连

系统里有 Settings 应用。进入 Settings 后找 Wi-Fi 页面，扫描并连接热点。

如果要从串口或代码里预置 Wi-Fi，需要另写配置逻辑，不建议直接改 NVS。

## 当前验证状态

已在 `/dev/ttyACM0` 设备上完成：

- 构建通过。
- 固件刷入通过。
- bootloader、partition table、LittleFS、app hash 校验通过。
- 系统启动正常。
- 用户实机确认：顶部白边消失。

当前源码构建状态：

- 已构建通过：首页圆角安全区、放大状态胶囊、真实电量读取、Wi-Fi 状态、时间同步、应用入口、Settings / App Store / Files / OTA 更新 App。
- 生成产物：
  - `watch_os/build/watch_os.bin`
  - `watch_os/build/littlefs_data.bin`

最近一次刷入：

- 日期：2026-08-08
- 串口：`/dev/ttyACM0`
- 方式：`idf.py -p /dev/ttyACM0 app-flash`
- 结果：只写入 app 分区 `0x20000 watch_os.bin`，hash 校验通过，最后 `Hard resetting via RTS pin... Done`。
- 保留内容：未写 `nvs` 和 `littlefs_data`，因此 Wi-Fi 记录、App Store 下载 App、用户文件应保留。
