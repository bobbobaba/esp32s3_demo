# 更新记录

本文件记录公开仓库中的主要更新内容。每次功能变更、UI 调整、硬件支持或文档更新都应同步追加简要说明。

## 2026-07-23

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
