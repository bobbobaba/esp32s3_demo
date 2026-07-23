# ESP32-S3 AI Watch UI

这是一个基于 ESP32-S3 N16R8 的小屏 AI 终端固件项目，使用 128x128 ST7735 屏幕、四个按键、I2S 麦克风、MAX98357A 喇叭、MPU6050 传感器和板载 RGB LED。

项目当前仍处于快速迭代阶段：核心功能已经可用，UI 还在从手写绘制逐步迁移到 LVGL/EEZ Studio 的可视化设计流程。

## 功能概览

- 128x128 ST7735 表盘首页
- 四按键菜单系统
- 手机扫码 Wi-Fi 配网页
- 天气/服务器状态展示接口
- AI 语音通话模式
  - 自动监听
  - 检测停顿后自动提交
  - 播放 AI 回复
  - 播放完成后自动继续监听
- I2S 麦克风录音
- MAX98357A I2S 喇叭播放
- 板载 RGB LED 控制和特效
- MPU6050 传感器页面
  - 原始加速度/陀螺仪/温度
  - 姿态角
  - 水平仪
  - 步数/估算里程
  - 摇动、抬手、静止、疑似跌落/撞击事件
  - 零位校准
- UI 页面关系蓝图：`docs/ui-flow.md`

## 最新更新

- 完整更新记录见：[CHANGELOG.md](CHANGELOG.md)
- 最新一次更新：实现服务器 OTA 检查/下载/写入流程，设置页显示 OTA 状态。
- UI 迁移进度：已完成 `UiPage` 状态机和 `dispatchButton()` 按键调度，正在拆分页面静态布局和动态刷新。

## 当前功能更新摘要

- 新增 AI 半双工通话模式：`P7 CALL` 开始，`P4` 退出。
- AI 通话不再需要每轮手动停止，检测到说话停顿后自动提交。
- MPU 页面改为局部刷新，避免整屏闪烁导致文字看不清。
- MPU 断线时不会卡死页面，SDA/SCL 异常会显示 `MPU6050 OFF`，并保持按键可退出。
- MPU 事件触发 LED 改为可配置，默认关闭；在 `EVENT` 页面用 `P6` 切换。
- 增加 Wi-Fi 设置二维码页面，手机可查看网络状态并重新配网。
- 新增 `docs/ui-flow.md`，用于后续迁移到 EEZ Studio/EEZ Flow。
- 公开仓库版本已脱敏：不包含真实服务器地址、Wi-Fi 密码、服务账号密码、token 或私钥。

## 快速开始

1. 安装 PlatformIO。
2. 按 [docs/hardware-wiring.md](docs/hardware-wiring.md) 完成接线。
3. 编译固件：

```bash
pio run
```

4. 连接 ESP32-S3 串口并烧录：

```bash
pio run --target upload --upload-port /dev/ttyACM0
```

5. 首次启动后连接 `ESP32S3-Setup` 热点，手机打开 `http://192.168.4.1/` 配网。

## 项目框架

```text
esp32s3_wifi_setup/
├── boards/
│   └── esp32-s3-n16r8-qspi.json      # 自定义 ESP32-S3 N16R8 QSPI 板卡配置
├── docs/
│   ├── eez-studio.md                 # EEZ Studio/LVGL 接入计划
│   ├── hardware-wiring.md            # 硬件接线说明
│   ├── ota-api.md                    # OTA 接口设计
│   └── ui-flow.md                    # 页面/按钮/状态机蓝图
├── src/
│   ├── main.cpp                      # 主固件逻辑
│   ├── lv_conf.h                     # LVGL 配置
│   ├── mi_style_watchface.h          # 表盘资源
│   ├── mi_style_watchface_preview.png
│   ├── radar_watchface.h             # 表盘资源
│   └── OPEN_SOURCE_UI_NOTICE.md      # 开源 UI 资源说明
├── platformio.ini                    # PlatformIO 工程配置
├── .gitignore
└── README.md
```

## 软件架构

当前 `main.cpp` 里主要包含这些模块：

- 硬件初始化
  - ST7735 SPI 屏幕
  - LVGL 显示缓冲
  - 四个按键
  - RGB LED
  - MPU6050 I2C
  - I2S 麦克风/喇叭
- 网络配置
  - ESP32 AP 配网热点
  - Web 配网页
  - DNS captive portal
- 数据服务
  - 登录服务接口
  - 天气接口
  - 服务器状态接口
  - AI 语音接口
- UI 页面
  - 首页
  - 菜单
  - 设置
  - Wi-Fi 配网
  - 服务器状态
  - LED 控制
  - AI 通话
  - MPU 数据/姿态/水平仪/里程/事件
- 后台任务
  - 天气刷新
  - 服务器状态刷新
  - OTA 更新检查
  - 语音录音/上传/播放
  - MPU 手势/事件检测
  - LED 特效

当前页面状态已经收口为单一 `UiPage` 状态机，后续会继续把按键分发和页面渲染迁移到 LVGL/EEZ Studio 生成的页面代码。

## 硬件接线

### ST7735 128x128 屏幕

| 屏幕引脚 | ESP32-S3 |
| --- | --- |
| LED | GPIO15 |
| SCL/SCK | GPIO12 |
| SDA/MOSI | GPIO11 |
| DC/RS | GPIO9 |
| CS | GPIO10 |
| RST | GPIO8 |
| VCC | 3.3V |
| GND | GND |

### 四个按键

按键为低电平触发，固件使用内部上拉。

| 按键 | ESP32-S3 |
| --- | --- |
| P4 | GPIO4 |
| P5 | GPIO5 |
| P6 | GPIO6 |
| P7 | GPIO7 |

### I2S 麦克风

| 麦克风信号 | ESP32-S3 |
| --- | --- |
| BCLK | GPIO16 |
| WS/LRCLK | GPIO17 |
| SD/DOUT | GPIO18 |
| VCC | 3.3V |
| GND | GND |

### MAX98357A 喇叭功放

| MAX98357A | ESP32-S3 |
| --- | --- |
| BCLK | GPIO16 |
| LRC/WS | GPIO17 |
| DIN | GPIO14 |
| VIN | 3.3V 或 5V，按模块规格选择 |
| GND | GND |

### MPU6050

| MPU6050 | ESP32-S3 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| SCL | GPIO41 |
| SDA | GPIO42 |
| INT | 可选 GPIO13 |
| XDA | 不接 |
| XCL | 不接 |
| ADC | 不接 |

## 按键操作

### 全局

| 按键 | 作用 |
| --- | --- |
| P4 | 返回/退出当前页面 |
| P5 | 上一个/减小/清零，取决于页面 |
| P6 | 下一个/增加/切换，取决于页面 |
| P7 | 确认/开始/校准，取决于页面 |

### 菜单页

| 按键 | 作用 |
| --- | --- |
| P4 | 返回首页 |
| P5 | 上一个菜单项 |
| P6 | 下一个菜单项 |
| P7 | 打开当前菜单项 |

### AI 通话页

| 按键 | 作用 |
| --- | --- |
| P4 | 退出通话 |
| P5 | 音量减 |
| P6 | 音量加 |
| P7 | 开始通话 |

### MPU 页面

| 页面 | 按键 | 作用 |
| --- | --- | --- |
| MPU DATA / ANGLE / LEVEL / ODO / EVENT | P7 | 零位校准 |
| ODO | P5 | 清步数 |
| EVENT | P5 | 清事件计数 |
| EVENT | P6 | 开关 MPU 事件 LED 反馈 |

## 构建

安装 PlatformIO 后执行：

```bash
pio run
```

烧录：

```bash
pio run --target upload --upload-port /dev/ttyACM0
```

如果串口不同，先查看设备：

```bash
pio device list
```

## 私有配置和脱敏说明

公开仓库不包含真实服务器地址、Wi-Fi 密码、服务账号密码、token 或私钥。

固件中的服务地址通过编译宏配置：

```cpp
#ifndef SERVICE_BASE_URL
#define SERVICE_BASE_URL "http://example.invalid"
#endif
```

本地使用时可以在私有且被忽略的 PlatformIO 配置中加入：

```ini
build_flags =
    -DSERVICE_BASE_URL=\"http://example.invalid/base-path\"
```

也可以按需添加：

```ini
build_flags =
    -DPROVISION_SSID=\"your_wifi\"
    -DPROVISION_PASSWORD=\"replace_me\"
    -DPROVISION_SERVICE_USER=\"your_user\"
    -DPROVISION_SERVICE_PASSWORD=\"replace_me\"
    -DSERVICE_BASE_URL=\"http://example.invalid/base-path\"
```

不要把真实值提交到 Git。

已忽略的敏感/本地文件包括：

- `.env`
- `.env.*`
- `*.pem`
- `*.key`
- `secrets.*`
- `credentials.*`
- `platformio_override.ini`
- `local_*.ini`
- `.pio/`

## UI 迁移计划

当前 UI 由手写 ST7735 绘制和部分 LVGL 组成。后续计划：

1. 已完成：用 `UiPage` 状态机替换多个 `showXxxPage` 布尔变量。
2. 已完成：把按键统一成 `dispatchButton()`。
3. 进行中：把每个页面拆成静态布局和动态数据刷新；AI 通话页、菜单页、设置页、服务器页、LED 页、Wi-Fi 页已拆分或缓存，MPU 页面已有动态刷新基础。
4. 使用 EEZ Studio/EEZ Flow 重新设计界面和页面跳转关系。
5. 导出 LVGL 页面代码，逐步替换手写 UI。

页面关系图和迁移蓝图见：

[docs/ui-flow.md](docs/ui-flow.md)

EEZ Studio 接入说明见：

[docs/eez-studio.md](docs/eez-studio.md)

OTA 接口设计见：

[docs/ota-api.md](docs/ota-api.md)

## 许可和资源说明

项目代码准备作为开源固件发布。部分 UI 资源来源说明见：

[src/OPEN_SOURCE_UI_NOTICE.md](src/OPEN_SOURCE_UI_NOTICE.md)
