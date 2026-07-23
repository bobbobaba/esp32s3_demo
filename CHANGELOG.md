# 更新记录

本文件记录公开仓库中的主要更新内容。每次功能变更、UI 调整、硬件支持或文档更新都应同步追加简要说明。

## 2026-07-23

### 接入 EEZ 生成 UI 到固件

- 将 EEZ 导出的 LVGL 页面代码接入 `src/ui`，主固件默认启用 EEZ UI 基础页面。
- 新增 `UiPage` 到 EEZ `ScreensEnum` 的映射：首页、菜单、AI 通话、设置、服务器、LED、Wi-Fi 配网和 MPU 数据页优先使用 EEZ 页面。
- 将天气、服务器、AI 状态、音量、OTA、Wi-Fi、LED 和 MPU 数据同步到 EEZ 动态 label。
- MPU 水平仪、姿态角、里程计和事件页暂时保留原手写 UI，作为复杂传感器页面的稳定兜底。

### 增加 EEZ Studio 可视化 UI 草图

- 新增 `eez-ui/esp32s3_wifi_setup/esp32s3_wifi_setup.eez-project`，画布尺寸设为 128x128。
- 在 EEZ 项目中预建首页、菜单、AI 通话、设置、服务器、LED、Wi-Fi 配网和 MPU 数据 8 个页面。
- 新增 EEZ 导出的 LVGL UI 骨架代码，包含页面枚举、页面创建函数和可更新的动态 label 对象。
- 忽略 EEZ Studio 自动生成的运行/构建缓存文件，仓库只保留可维护的项目和源码。
- 当前 EEZ UI 仍是设计草图，主固件暂时继续使用现有手写 LVGL 页面。

### 实现服务器 OTA 流程

- 新增 `serviceOta()`，联网空闲时定时检查服务器设备配置。
- 新增 `checkAndApplyOta()`，读取 `device-config` 中的固件版本和下载地址。
- 新增 `applyOtaFirmware()`，使用 `Update.writeStream()` 下载并写入新固件。
- 设置页显示 OTA 状态，例如 `OTA OFF`、`OTA OK`、`OTA WRITE`、`OTA REBOOT`。
- 公开版默认 `example.invalid` 会跳过 OTA，不包含真实服务器地址。

### 批量拆分页面动态刷新和完善开源文档

- 设置页拆分为框架和动态刷新，音量变化只刷新音量数字和进度条。
- 服务器页拆分为框架和动态刷新，运行时间、负载、内存、磁盘只刷新数据区域。
- LED 页拆分为框架和动态刷新，灯效切换和动画颜色只刷新预览区和选中状态。
- Wi-Fi 二维码页复用 LVGL 对象，避免重复清屏重建二维码页面。
- 增加音量持久化，按键调整后的音量会保存到 ESP32 Preferences。
- 新增硬件接线、EEZ Studio 接入计划和 OTA API 设计文档。

### 拆分菜单页动态刷新

- 将菜单页拆分为 `renderMenuFrame()` 和 `renderMenuList()`。
- 新增菜单渲染缓存，记录上次选中项和列表窗口起点。
- 菜单上下选择时，如果列表窗口没有滚动，只重画原选中行和新选中行。
- 为后续 EEZ Studio 菜单页面接入保留更清晰的列表刷新入口。

### 拆分 AI 通话页动态渲染

- 将 AI 通话页拆分为页面框架和动态内容区。
- 新增 `VoiceRenderMode`，区分上传、录音、播放、结果和待机状态。
- 录音计时只刷新中间内容区，减少整屏清空造成的闪烁。
- 切换页面时重置语音页渲染缓存，避免残影或状态误判。

### 增加统一按键调度

- 将 `serviceButtons()` 收口为按键扫描和消抖逻辑。
- 新增 `dispatchButton(index)` 统一入口。
- 按页面拆分按键处理函数，便于后续接入 EEZ Studio/EEZ Flow 页面回调。

### 重构 UI 页面状态

- 用单一 `UiPage currentUiPage` 替换多个 `showXxxPage` 布尔变量。
- 新增 `setUiPage()` 和 `sensorPageVisible()`。
- 页面跳转状态更集中，便于后续迁移到 LVGL/EEZ 生成页面。

### 开源仓库初始化

- 初始化 PlatformIO ESP32-S3 固件工程。
- 增加中文 README、项目结构、硬件接线、构建说明和 UI 迁移计划。
- 增加 `docs/ui-flow.md`，记录页面跳转图、按钮逻辑图和 EEZ Studio 迁移蓝图。
- 公开版本完成脱敏，不包含真实服务器地址、Wi-Fi 密码、服务账号密码、token 或私钥。
