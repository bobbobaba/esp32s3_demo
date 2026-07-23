# EEZ Studio / EEZ Flow 接入计划

当前固件已经把页面状态和按键入口收口，下一步可以把手写 UI 逐步替换成 EEZ Studio 导出的 LVGL 页面。

## 当前 EEZ 项目

EEZ Studio 打开这个文件：

```text
/home/bo/esp32s3_wifi_setup/eez-ui/esp32s3_wifi_setup/esp32s3_wifi_setup.eez-project
```

当前项目已经配置为 128x128 LVGL 画布，并预建这些页面：

| EEZ 页面 | 用途 |
| --- | --- |
| `Home` | 表盘首页 |
| `Menu` | 四按键菜单 |
| `AI_Call` | AI 通话 |
| `Settings` | 设置 |
| `Server` | 云服务器状态 |
| `LED` | 板载 LED 控制 |
| `WiFi_Setup` | 手机扫码配网 |
| `MPU_Data` | MPU6050 数据 |

导出的 LVGL 骨架代码在：

```text
/home/bo/esp32s3_wifi_setup/eez-ui/esp32s3_wifi_setup/src/ui
```

注意：这个目录目前是 UI 设计草图和后续迁移样板，还没有接入 `src/main.cpp` 的实际固件渲染流程。

## 固件侧入口

| 固件接口 | 用途 |
| --- | --- |
| `UiPage currentUiPage` | 当前页面状态 |
| `setUiPage(UiPage page)` | 页面切换入口 |
| `dispatchButton(size_t index)` | 四个实体按键统一入口 |
| `renderWeather()` | 当前 UI 路由和刷新入口 |
| `renderXxxFrame()` | 页面静态布局 |
| `renderXxxDynamicOnly()` | 页面动态数据刷新 |

## 建议页面

| EEZ Screen | 对应固件页面 |
| --- | --- |
| `screen_home` | `UiPage::Watch` |
| `screen_menu` | `UiPage::Menu` |
| `screen_ai_call` | `UiPage::Voice` |
| `screen_settings` | `UiPage::Settings` |
| `screen_wifi_setup` | `UiPage::WifiSetup` |
| `screen_server` | `UiPage::Server` |
| `screen_led` | `UiPage::Light` |
| `screen_mpu_data` | `UiPage::MpuData` |
| `screen_mpu_angle` | `UiPage::MpuAngle` |
| `screen_mpu_level` | `UiPage::MpuLevel` |
| `screen_odometer` | `UiPage::MpuOdometer` |
| `screen_motion_event` | `UiPage::MpuMotion` |

## 按键映射

EEZ 页面不直接读 GPIO。实体按键仍然由固件扫描，然后调用页面动作：

| 按键 | index | 默认动作 |
| --- | --- | --- |
| P4 | 0 | 返回/退出 |
| P5 | 1 | 上一个/减小/清零 |
| P6 | 2 | 下一个/增加/切换 |
| P7 | 3 | 确认/开始/校准 |

## 迁移顺序

1. 在 EEZ Studio 中创建 128x128 LVGL 项目。
2. 按 `docs/ui-flow.md` 创建页面和页面跳转关系。
3. 先迁移 `screen_menu`、`screen_settings`、`screen_led`，这些页面状态最简单。
4. 再迁移 `screen_ai_call`，保留现有 `VoiceRenderMode` 状态。
5. 最后迁移 MPU 页面，保留现有动态刷新节奏，避免传感器数据闪烁。

## 命名约定

- 静态对象用页面名前缀，例如 `menu_label_title`。
- 动态数据标签用 `value_` 前缀，例如 `settings_value_volume`。
- 页面动作回调用 `action_` 前缀，例如 `action_menu_next`。

## 注意事项

- 128x128 屏幕空间很小，页面内不要放长句说明。
- 底部按键提示保持一行或两段短文本。
- 动态数据只更新 label/bar/arc，不重新创建 screen。
- 生成代码不要写入真实服务器地址、Wi-Fi 密码或 token。
