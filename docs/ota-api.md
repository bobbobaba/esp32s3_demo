# OTA 接口设计

当前公开仓库不包含真实服务器地址。OTA 功能通过私有 `SERVICE_BASE_URL` 编译宏启用，并使用 Bearer token 访问服务端。

公开版默认 `SERVICE_BASE_URL` 是 `http://example.invalid`，固件会显示 `OTA OFF` 并跳过 OTA 检查。

## 设备配置接口

固件预留接口：

```text
GET {SERVICE_BASE_URL}/api/v1/device-config
Authorization: Bearer <token>
```

建议返回：

```json
{
  "firmware": {
    "version": "1.0.1",
    "url": "https://example.invalid/firmware/esp32s3.bin",
    "sha256": "optional_sha256",
    "mandatory": false,
    "notes": "简短更新说明"
  }
}
```

## 固件升级流程

1. 设备联网后登录服务端获取 Bearer token。
2. 定时请求 `device-config`。
3. 比较服务端 `firmware.version` 和本地 `FIRMWARE_VERSION`。
4. 如果发现新版本，进入 OTA 状态：检查、下载、写入、重启。
5. 失败时保留旧固件，屏幕显示简短失败原因。

## 固件侧状态

当前固件已实现：

- `serviceOta()` 后台定时检查。
- `checkAndApplyOta()` 获取设备配置并判断版本。
- `applyOtaFirmware()` 下载固件并通过 `Update.writeStream()` 写入。
- 设置页显示 OTA 状态，例如 `OTA OFF`、`OTA OK`、`OTA WRITE`、`OTA REBOOT`。

## 屏幕状态建议

| 状态 | 屏幕文案 |
| --- | --- |
| 检查更新 | `OTA CHECK` |
| 无更新 | `OTA OK` |
| 下载中 | `OTA DOWN` |
| 写入中 | `OTA WRITE` |
| 校验失败 | `OTA HASH` |
| 空间不足 | `OTA SPACE` |
| HTTP 失败 | `OTA HTTP` |
| 升级成功 | `OTA REBOOT` |

## 安全要求

- 公开仓库不提交真实 OTA 地址。
- 不在代码中硬编码服务器账号、密码、token 或私钥。
- HTTPS 和 SHA-256 校验应作为正式 OTA 的默认方案。
- HTTP 明文只用于本地调试或可信内网。
