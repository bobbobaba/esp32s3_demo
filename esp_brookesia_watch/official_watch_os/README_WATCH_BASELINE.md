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
- 版本：`0.1.67`
- 大小：约 3.9 MB，二进制大小 `0x3de660`
- SHA256：`157e0e3e6ec15d56d275cc37a6417805d1265b15aea29db28dd1573886e3fb55`
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
- Cloud：云服务器状态监控 App，读取 private OTA backend 设备状态、天气、设备配置摘要。
- Quota：API 额度监控 App，读取 private OTA backend `api-usage-status` 摘要；本机 ccswitch 同步只上传脱敏后的余额/用量字段。
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

- `POST <PRIVATE_OTA_BASE_URL>/api/v1/auth/login`
- `GET  <PRIVATE_OTA_BASE_URL>/api/v1/device-config`
- `PUT  <PRIVATE_OTA_BASE_URL>/api/v1/device-status`
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

## 2026-08-12 0.1.67 watch home quota loading state

- WatchHome / Quota：
  - 首页 Quota 摘要在本机缓存还没有真实 token 时，不再显示 `0 tok`；
  - `quota_refresh_async()` 正在运行时显示 `Quota loading...`，没有缓存且未刷新时显示 `Quota --`；
  - 只有云端真实 usage 刷新成功并写入 NVS 后，才显示今日 token 和本月/今日成本。
- Release：`releases/0.1.67-watch-os-brookesia.bin`
- SHA256：`157e0e3e6ec15d56d275cc37a6417805d1265b15aea29db28dd1573886e3fb55`
- OTA 固件 ID：线刷验证版本，未推 OTA
- 已本地构建通过：app 分区余量约 65%；固件镜像版本头确认为 `0.1.67`。
- 本版本用于 OTA 失败后的线刷验证；不刷 NVS / LittleFS / SD / otadata。

## 2026-08-12 0.1.66 ccswitch live config / usage-only display

- Quota / ccswitch：
  - 同步脚本兼容新版 CC Switch 当前 provider 配置，支持从 `settings_config.auth` 提取本机真实 key、从 `settings_config.config` 提取 `base_url`，用于尝试 provider live usage 查询；
  - 当前 provider 没有 live quota 或 `limit_monthly_usd` 时，不显示假余额，继续上传并显示本机真实 `proxy_request_logs` / `usage_daily_rollups` 汇总；
  - 上传 raw 数据时不包含 key/token/secret，也不包含第三方 provider endpoint，只保留 `usage_endpoint_configured` 布尔诊断；
  - Quota App 记录并显示 `usage_only` 状态，首页 Quota 摘要也持久化 status，避免把 `remaining_amount=0` 误显示为真实余额；
  - 成本字段兼容 `actual_cost` / `total_cost` / `cost_usd`，适配不同开源/服务器 token 用量 UI 的字段风格。
- Release：`releases/0.1.66-watch-os-brookesia.bin`
- SHA256：`9aa8c98d9fe9aec31717a31a2bd9acd6cfca5cf00e998b30514d7108713172ce`
- OTA 固件 ID：`144`
- 已本地构建通过并上传 OTA：app 分区余量约 65%；固件镜像版本头确认为 `0.1.66`；远端 bin 与本地 release SHA256 一致。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。

## 2026-08-12 0.1.64 reliability polish / Files diagnostics

- WiFi：
  - 确认配网网页 URL decode 不再重复写入 `%xx` 解码字符，避免特殊字符/中文 SSID/密码被改坏；
  - 自动连接和扫描任务启动前增加 internal heap largest block 检查；
  - 自动连接运行时拒绝并发扫描，避免 WiFi driver / scan record / task 内存竞争。
- WatchHome：
  - 保存 `_clock_timer`，进入首页前清理旧 timer；
  - `back()` / `close()` 删除 timer 并清空首页 LVGL 对象指针，降低反复切换后残留刷新导致的卡屏风险。
- OTA：
  - `_task` 未清理完成时拒绝再次启动 OTA 操作；
  - `runOperation()` 只负责业务完成后的按钮状态，task 入口统一释放 `_operation_running` 和 `_task`，避免重复点击导致状态错乱。
- Music：
  - 云任务运行时阻止本地播放，本地播放任务运行时阻止云下载；
  - 本地播放返回后重新扫描音乐目录，避免 SD 文件状态变化后列表过期。
- Files：
  - 增加 `SD Music` / `SD Logs` / `LittleFS` 子入口，方便直接查看音乐目录、日志目录和 LittleFS 根目录。
- Release：`releases/0.1.64-watch-os-brookesia.bin`
- SHA256：`c568375c03f24df44e28608b578332787fae4e429a071214b493598c265eb04c`
- OTA 固件 ID：`142`
- 已本地构建通过并上传 OTA：app 分区余量约 65%；固件镜像版本头确认为 `0.1.64`；远端 bin 与本地 release SHA256 一致。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。

## 2026-08-12 0.1.65 ccswitch quota / UI task safety

- WiFi：
  - 扫描结束后恢复非配网状态下的自动连接重试；
  - 当手动扫描打断 STA 连接后，如果已有保存网络且未连接，会请求重新自动连接，避免长期停留在未连接状态。
- Music：
  - 本地播放后台任务不再直接调用 `scanTracks()` 或间接更新 LVGL 控件；
  - 播放完成只标记 `_list_dirty`，由 LVGL timer 在 UI 线程刷新列表和状态，降低点击 `Play` 后闪退/卡屏风险；
  - 云下载完成后对 `_cloud_tracks` 和 `_list_dirty` 的更新加 mutex 保护。
- Quota / ccswitch：
  - 修复新版 ccswitch SQLite schema 兼容，同步脚本读取真实 `providers`、`proxy_request_logs`、`usage_daily_rollups`；
  - 当前 provider 没有 `limit_monthly_usd` / live quota 时，上传 `usage_only`，显示真实今日/本月成本、请求数、tokens、模型排行，不伪造剩余额度；
  - Quota App 兼容新结构 `raw.usage` / `raw.model_stats`，同时保留旧结构 `raw.usage_response.usage`；
  - 首页 Quota 摘要在 `usage_only` 时显示本月成本和今日成本/token，不再误显示 `$0.00` 余额。
- Release：`releases/0.1.65-watch-os-brookesia.bin`
- SHA256：`42934032661dff1ff05ae3c148a4af2e82ffdbb37edacecbb97e90c59c933dc8`
- OTA 固件 ID：`143`
- 已本地构建通过并上传 OTA：app 分区余量约 65%；固件镜像版本头确认为 `0.1.65`；远端 bin 与本地 release SHA256 一致。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。

## 2026-08-12 0.1.63 task cleanup / home render stability

- 修复 `xTaskCreateWithCaps()` 创建的短生命周期任务退出时使用普通 `vTaskDelete()` 的问题，改为 `vTaskDeleteWithCaps()`：
  - Settings WiFi setup / scan；
  - WiFi autoconnect / DNS / setup guard；
  - Weather / Quota / Cloud / OTA；
  - Music cloud / local play；
  - Sensors。
- 保留普通 `xTaskCreate()` 任务的 `vTaskDelete()`，不误改音频测试和频谱任务。
- 首页时间取消 `transform_zoom` 缩放绘制，改为原生 `lv_font_montserrat_48` + 固定宽度居中，避免缩放后的 glyph bounds 超出圆角安全区导致横纹/脏刷新。
- OTA 启动上报任务开始时调用 `esp_ota_mark_app_valid_cancel_rollback()`，当前 bootloader 未启用 rollback 时只记录非致命 warning。
- Release：`releases/0.1.63-watch-os-brookesia.bin`
- SHA256：`7f0f0ccf4949d82d05e09fac784263f665574921ea3d0377f33e01546d082297`
- OTA 固件 ID：待上传
- 已本地构建通过：app 分区余量约 65%；固件镜像版本头确认为 `0.1.63`。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。

## 2026-08-12 0.1.62 stability polish / startup report

- 新增启动后自动上报：
  - 当前固件版本；
  - running partition；
  - boot partition；
  - next OTA partition；
  - reset reason / heap / WiFi 状态。
- OTA App 和云端状态上报增加 `boot_partition` 字段，页面分区摘要从 `Run / Next` 改为 `Run / Boot / Next`。
- Settings：
  - `About` / `System Health` 增加 `Run / Boot / Next` 分区显示；
  - `About` 卡片重复注册修复；
  - Sensor / Audio / About 卡片高度调整，减少文字裁切；
  - 非忙时刷新周期从 3 秒改为 5 秒。
- 首页：
  - WiFi 未连接但有保存网络时显示 `WiFi saved`；
  - 无保存网络时显示 `WiFi off`；
  - 天气离线时显示 `weather offline`，避免占位文案误导。
- Music：
  - 本地播放前检查 MP3/WAV 文件头；
  - 无效文件不再进入播放任务；
  - 本地播放 task 改用 PSRAM task；
  - task 创建失败时显示 internal heap / largest block。
- Release：`releases/0.1.62-watch-os-brookesia.bin`
- SHA256：`09c768e93c97395ab3d0085e2152beae59e4e15dec5939311b8a5fde7d3a4af8`
- OTA 固件 ID：`140`
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。

## 2026-08-12 0.1.61 OTA final-stage reliability diagnostics

- 修复 OTA 末尾可观测性和稳定性：
  - OTA 下载 buffer 从 task 栈改为堆内存；
  - buffer 从 8KB 降到 4KB；
  - OTA task 栈从 24KB 降到 20KB；
  - 减少 OTA 下载完成后 `esp_ota_end()` 阶段的栈/内部内存压力。
- OTA 末尾状态上报拆分：
  - `OTA校验开始`；
  - `OTA校验完成`；
  - `OTA切换启动分区`；
  - `OTA完成，准备重启`。
- `esp_ota_set_boot_partition()` 后读取 boot partition 校验目标分区，若不一致则上报失败，不直接重启。
- 目的：定位 0.1.51 设备下载 0.1.60 后停在 `OTA校验并切换启动分区` 的问题，确认失败点是在 `esp_ota_end()`、启动分区切换，还是重启后启动槽。
- Release：`releases/0.1.61-watch-os-brookesia.bin`
- SHA256：`105e4776ecd9f428903000aab1cafd6337e5b29358affa4531af4f682d51b135`

## 2026-08-12 0.1.60 settings health visibility fix

- 修复 Settings 页面里不容易看到 `Health` 页的问题：
  - Settings 菜单第一项改为 `System Health`；
  - 入口说明改为 `Diagnostics: heap, WiFi, SD, audio, OTA`；
  - 不改 Health 诊断内容，只修入口可见性。
- 状态栏 WiFi 图标刷新周期从 1 秒改为 5 秒，减少无意义 UI 刷新。
- Weather App 改为复用 `watch_weather` 共享天气服务，和首页使用同一份天气缓存；删除页面内重复登录、HTTP、JSON 解析逻辑。
- 清理 Settings `run()` 中 `return true` 之后的不可达旧 UI 构建代码，减少维护风险和无效固件体积。
- 首页主时间显示放大约 17%，保持在圆角安全区内，不改变亮度和息屏策略。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。

## 2026-08-12 0.1.59 one-shot diagnostics optimization

- Settings `Health` 页继续增强：
  - WiFi 诊断；
  - Storage 缓存诊断；
  - Audio 状态；
  - OTA 目标分区容量；
  - heap / PSRAM / task 状态。
- 新增 `watch_connectivity::wifi_diag_text()`：
  - WiFi state；
  - connected / provisioning；
  - SSID / RSSI；
  - 保存网络数量；
  - 最近失败原因和 disconnect reason；
  - 连接耗时；
  - autoconnect/manual disconnect 状态；
  - internal heap / largest block。
- 新增 `watch_storage::storage_diag_text()`：
  - 只读当前缓存状态；
  - 不主动挂载 SD 或 LittleFS；
  - 避免 Health 页定时刷新时触发存储阻塞。
- WiFi 扫描 / 配网 task 创建失败时，页面显示 internal heap 和 largest block。
- OTA task 创建失败时，detail 行显示 internal heap、largest block 和 PSRAM 余量。
- 本版本仍只更新 app 固件；不刷 NVS / LittleFS / SD / otadata，保留 WiFi 记录、设置项和 AppStore 下载内容。
- 已上传并推送 OTA：固件 ID `137`，URL `<PRIVATE_OTA_BASE_URL>/firmware/0.1.59-50b5952490664acd8ddff5690fbcc8bf.bin`，远端 bin 与本地 release SHA256 一致。
- Release：`releases/0.1.59-watch-os-brookesia.bin`
- SHA256：`f67d2960dad61a3f0153e0202152a5438d103feb4538dc9c2ea481591d4bc882`

## 2026-08-11/12：0.1.43 到 0.1.58 更新补录

这段记录是 2026-08-12 补全的历史记录，依据本地 release 文件、SHA256、源码文件修改时间和现有功能状态整理。`0.1.47` / `0.1.48` 在当前 `releases/` 目录没有保留手表固件文件，因此标为未保留正式 watch release。

### 0.1.58 settings health / OTA diagnostics

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
- Release：`releases/0.1.58-watch-os-brookesia.bin`
- SHA256：`9a1db63f382030b7a2fcfdc5d7d86469a1f3bc153bd652dc99676c75fa3bedfa`

### 0.1.57 memory pressure reduction

- 继续降低内部 RAM 压力，重点针对此前 WiFi 扫描/配网页面出现的 `no memory` / task create failed 问题。
- `watch_connectivity`、`watch_power`、`watch_weather` 和主入口的后台任务做内存与任务栈优化：
  - PWR 按键轮询、电池缓存、WiFi 自动连接、配网 DNS、配网 guard、首页天气刷新继续放在后台；
  - WiFi guard 任务栈从 3072 降到 2048；
  - 尽量使用 PSRAM task stack，减少内部 RAM 占用。
- 不改显示驱动，不改亮度/息屏时间，不改 NVS/LittleFS/SD 数据，不启用 light sleep。
- Release：`releases/0.1.57-watch-os-brookesia.bin`
- SHA256：`9bc759153a02d3dec6bb5cd3b1f6eca1fb94d9a41be0281eda40cc1d86d96d69`

### 0.1.56 UI non-blocking refresh pass

- 对多个 App 页面做后台化/非阻塞刷新整理，减少进入页面时 UI 卡顿：
  - Alarm；
  - Music；
  - Sensors；
  - Cloud；
  - Weather；
  - Quota。
- `watch_display` 同步做显示状态读写整理，避免页面直接阻塞硬件访问。
- 目标是降低设置、OTA、天气、配网等页面“横纹/卡屏/按行刷新”的概率。
- Release：`releases/0.1.56-watch-os-brookesia.bin`
- SHA256：`805e508a290eff354ae4dfe4b3976710f4a3d48266581393ab7c3a1f6a84d17d`

### 0.1.55 release validation build

- 当前本地保留了 `0.1.55` release，但没有对应独立功能改动记录。
- 从源码时间线看，它位于 `0.1.54` 首页/天气显示修复和 `0.1.56` 多页面非阻塞刷新之间，作为中间验证构建保留。
- Release：`releases/0.1.55-watch-os-brookesia.bin`
- SHA256：`466c632d15c549310a2b0e5d622acfa750b8e15401f6d88fe5bf331a15a34cf9`

### 0.1.54 watch home display fix

- 修复表盘首页信息布局：
  - 首页 WiFi 显示改为 WiFi 名和信号强度，不再只显示泛化连接状态；
  - 天气信息排版继续收敛，避免顶部/右侧圆角裁切；
  - 保持 PWR 单击进入菜单/返回首页的既有逻辑。
- 主要改动集中在 `brookesia_app_watch_home`。
- Release：`releases/0.1.54-watch-os-brookesia.bin`
- SHA256：`80ced6af2c46cdcc1c16dfb98a13f6885dc2ed68b0c424e44d2d87c88db5ed8c`

### 0.1.53 WiFi setup integration

- Settings 继续接入 `watch_connectivity`：
  - 为二维码配网和手表端输入 WiFi 名/密码提供连接服务入口；
  - 将设置页面 WiFi 操作从 UI 层进一步拆到连接服务；
  - 为后续自动识别/自动切换已保存 WiFi 做接口准备。
- 主要改动集中在 `watch_connectivity.hpp` 和 Settings 组件注册。
- Release：`releases/0.1.53-watch-os-brookesia.bin`
- SHA256：`e589cfd0fbd08903afd3db18c5a40beeca681e2cb7b7117d2f5d32cdb093c75b`

### 0.1.52 build config / home component registration

- 调整工程构建配置和 WatchHome 组件注册：
  - `main/CMakeLists.txt`；
  - `sdkconfig.defaults`；
  - `brookesia_app_watch_home/CMakeLists.txt`。
- 用于保证当前 official_watch_os 工程里的首页 App、基础服务和后续功能组件按预期参与构建。
- Release：`releases/0.1.52-watch-os-brookesia.bin`
- SHA256：`9c3d5743df735f85f6fd6d212666036f25c3250149ddf11df9c1a699aed0b323`

### 0.1.51 quota service extraction

- 将 API 额度状态从 Quota App 页面中抽成 `watch_quota` 服务层：
  - 统一缓存 provider、balance、usage、limit、更新时间等摘要；
  - 避免 Quota App 页面直接长期持有网络/解析状态；
  - 方便首页后续选择显示 token/API 余额。
- Release：`releases/0.1.51-watch-os-brookesia.bin`
- SHA256：`a77c38780fb0ad0736026dc69b6be8b14b3120937af2fb5d61a962f4f0b8b3c6`

### 0.1.50 anniversary home integration

- 增强纪念日服务 `watch_anniversary`：
  - 纪念日配置继续保存在 NVS；
  - 支持为首页展示准备选中/启用项；
  - `Anniv` App 页面同步适配服务接口。
- 同时整理 Quota App 的组件注册。
- Release：`releases/0.1.50-watch-os-brookesia.bin`
- SHA256：`f7998d91ebd0f9136d7b08c55bc2da047b8a05715062914ca9162562fd6b32bc`

### 0.1.49 music/quota stability pass

- Music 相关：
  - 继续修复本地音乐播放链路；
  - `watch_audio` 侧整理播放状态和 codec 写入路径；
  - 避免点击本地播放时直接崩溃或卡死。
- Quota 相关：
  - Quota App 状态字段扩展，为更多真实 API 用量信息展示做准备。
- Release：`releases/0.1.49-watch-os-brookesia.bin`
- SHA256：`ddf16616f756119b3d3ce50e974568d8cc6a029e0d9928ead3ba4752932a7fa4`

### 0.1.48 skipped / not retained

- 当前 watch 工程的 `releases/` 目录没有 `0.1.48-watch-os-brookesia.bin`。
- 未找到对应 official_watch_os 正式 release 记录；不补写具体功能，避免把其他项目 ESP32_AUDIO_HUB 的 `0.1.48` 记录误写到手表系统里。

### 0.1.47 skipped / not retained

- 当前 watch 工程的 `releases/` 目录没有 `0.1.47-watch-os-brookesia.bin`。
- 未找到对应 official_watch_os 正式 release 记录；不补写具体功能，避免把其他项目 ESP32_AUDIO_HUB 的 `0.1.47` 记录误写到手表系统里。

### 0.1.46 release validation build

- 当前本地保留了 `0.1.46` release，但没有对应独立功能改动记录。
- 从生成时间看，它处在 `0.1.45` 后、`0.1.49` 音乐/Quota 稳定性修复前，作为中间验证构建保留。
- Release：`releases/0.1.46-watch-os-brookesia.bin`
- SHA256：`d6bdbcf55cf7941f8eaaf51d1a2fa9375c22132b44f725214e5c1b8dd6b4264f`

### 0.1.45 release validation build

- 当前本地保留了 `0.1.45` release，但没有对应独立功能改动记录。
- 从生成时间看，它紧随 `0.1.44` 设置/存储稳定性调整后生成，作为中间验证构建保留。
- Release：`releases/0.1.45-watch-os-brookesia.bin`
- SHA256：`73746452ec2f626d5369a4a71a5cf73ea07a9fe89a7fa4e7e18669317a95da3d`

### 0.1.44 settings / storage / ccswitch quota sync

- ccswitch API 用量同步默认读取本机 `~/.cc-switch/cc-switch.db`：
  - 当前 provider 来自 `providers.is_current`。
  - 月用量来自 `usage_daily_rollups.total_cost_usd`。
  - 月额度来自 `providers.limit_monthly_usd`。
  - 不读取、不上传 `settings_config` 中的 API key/token。
- 如果 ccswitch 没有配置月额度，Quota App 显示真实已用金额和 `limit not set`，不显示假剩余额度。
- Settings / Files / Storage 相关页面继续修复，避免进入设置页或文件页时崩溃；SD/TF 和 LittleFS 状态显示保持只读，不自动格式化数据。
- Release：`releases/0.1.44-watch-os-brookesia.bin`
- SHA256：`9c497dc7e4492bde983afa896291fbc13aaf6bd39e27d81c06dc38fbf00788ca`

### 0.1.43 no-fake-quota update

- Quota App 不再把 `not_synced` 显示为 `0% Used` 或 `0` 余额。
- private OTA backend quota 同步接口拒绝空/默认零值；没有真实采集数据时保持 `not_synced`。
- Release：`releases/0.1.43-watch-os-brookesia.bin`
- SHA256：`52607fbc0bd3904fd8f961eae60b9003646bc579af0703e7dc7523e6fcb3454a`

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
- 0.1.41 已上传并推送 OTA：固件 ID `122`，URL `<PRIVATE_OTA_BASE_URL>/firmware/0.1.41-2ac064b097ee42ccb04daf9603c76bac.bin`，远端 bin 与本地 release SHA256 一致。
- 当前 0.1.42 已构建通过，并生成 release：
  `releases/0.1.42-watch-os-brookesia.bin`
- 0.1.42 新增 `Cloud` 云服务器状态监控 App 和 `Quota` API 额度监控 App；两个 App 的 HTTP 请求都在后台 task 中执行，UI 线程只刷新缓存。
- 0.1.42 已上传并推送 OTA：固件 ID `123`，URL `<PRIVATE_OTA_BASE_URL>/firmware/0.1.42-bde44a478eb840e58be45fc1d3a01a90.bin`，远端 bin 与本地 release SHA256 一致。
- 未刷 NVS、LittleFS、otadata、bootloader、partition table。
