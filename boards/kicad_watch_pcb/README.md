# ESP32-S3 AI Watch Carrier PCB

这是当前 `esp32s3_wifi_setup` 固件对应的 KiCad 转接底板初版。

## 文件

- `esp32s3_watch_carrier.kicad_pro`: KiCad 9 项目文件
- `esp32s3_watch_carrier.kicad_pcb`: PCB 板文件
- `open_kicad_watch.sh`: 兼容启动脚本，强制使用 Mesa 软件 OpenGL，避免部分 NVIDIA GLX 环境下 PCB 编辑器闪退

## 当前设计范围

- ESP32-S3 开发板以 2.54mm 排针/杜邦线接入，中心排针 `J1` 标注全部固件使用 GPIO。
- ST7735 屏幕、I2S 麦克风、MAX98357A、MPU6050 以模块排针连接。
- P4/P5/P6/P7 四个按键已放置为板载轻触按键，低电平触发，另一端接 GND。
- J2/J3/J4/J5 外设电源脚旁已放置本地去耦电容，J6 电源入口已放置 bulk 电容位。
- 板框为 180mm x 120mm 的草图尺寸，便于先看布局和连线关系。

## 固件 GPIO 映射

| 功能 | GPIO |
| --- | --- |
| TFT LED | GPIO15 |
| TFT SCK | GPIO12 |
| TFT MOSI | GPIO11 |
| TFT DC | GPIO9 |
| TFT CS | GPIO10 |
| TFT RST | GPIO8 |
| P4/P5/P6/P7 | GPIO4/GPIO5/GPIO6/GPIO7 |
| I2S BCLK | GPIO16 |
| I2S WS | GPIO17 |
| I2S MIC SD | GPIO18 |
| I2S SPK DIN | GPIO14 |
| MPU SCL/SDA | GPIO41/GPIO42 |
| MPU INT | GPIO13 |

## 当前 PCB 约束

- 当前 A0 版本是可焊接验证底板，不是最终手表小尺寸压缩板。
- MAX98357A `VIN` 已接 `5V`，用于提高喇叭输出功率。
- MPU6050 I2C 使用当前固件定义的 `GPIO41=SCL`、`GPIO42=SDA`，不是 P7/P8。
- J1 顶部重复的 GND/3V3/5V 焊盘只作为机械/备用焊盘，不参与板上电源母线；电源母线从 J1 下方 GND/3V3/5V 引出。
- 已通过 KiCad DRC：0 条违规、0 个未连接项。

## 已落实的布线优化

- 5V、3V3、GND 使用加粗走线，普通信号线使用 0.25mm。
- I2S 麦克风和 MAX98357A 共用 BCLK/WS，SD 与 DIN 分开走线。
- ST7735、I2S、MPU6050、按键网络全部布通，未连接提示线清零。
- C1-C4 为外设 100nF 去耦电容位，分别靠近 J2/J3/J4/J5；C5 为 J6 输入 bulk 电容位。

## A1 制版前建议

- 按最终手表外壳尺寸重新压缩板框和器件坐标。
- 将 C1-C5 按装配能力选择 0402/0603/0805；当前 A0 偏向手工焊接友好。
- J6 电源入口增加防反接二极管、0805 自恢复保险丝和 TVS/ESD 保护。
- 屏幕、音频、I2C 外接排线信号预留 0603 串阻位。
- 若追求更低噪声，重排为屏幕区、音频区、传感器区、按键/电源区，并减少 I2S/SPI 高速线过孔。

## 后续需要确认

- ESP32-S3 开发板实际引脚排布和孔距。
- 屏幕、麦克风、MAX98357A、MPU6050 模块的实际排针方向。
- 最终手表外壳尺寸、安装孔位置、电池/充电模块位置。

## 打开方式

如果直接打开 KiCad/PCB Editor 闪退，使用：

```bash
./open_kicad_watch.sh
```

本机实测闪退点在 `libGLX_nvidia.so.0` 创建 OpenGL 上下文，脚本已设置 `__GLX_VENDOR_LIBRARY_NAME=mesa` 和 `MESA_LOADER_DRIVER_OVERRIDE=llvmpipe` 避开该问题。
