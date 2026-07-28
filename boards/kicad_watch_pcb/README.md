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

## 后续需要确认

- ESP32-S3 开发板实际引脚排布和孔距。
- 屏幕、麦克风、MAX98357A、MPU6050 模块的实际排针方向。
- 是否使用 5V 给 MAX98357A 供电，还是统一 3.3V。
- 最终手表外壳尺寸、安装孔位置、电池/充电模块位置。

## 打开方式

如果直接打开 KiCad/PCB Editor 闪退，使用：

```bash
./open_kicad_watch.sh
```

本机实测闪退点在 `libGLX_nvidia.so.0` 创建 OpenGL 上下文，脚本已设置 `__GLX_VENDOR_LIBRARY_NAME=mesa` 和 `MESA_LOADER_DRIVER_OVERRIDE=llvmpipe` 避开该问题。
