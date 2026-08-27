# bambu-monitor — ESP32 墨水屏 Bambu Lab 打印机监控

基于 **ESP32 + 4.2 英寸黑白墨水屏**的 3D 打印机实时监控与桌面信息面板。
数据来自 Home Assistant 的 Bambu Lab 官方集成，本地网络运行、无需云服务。

## 功能

三个界面，用 IO0 按键循环切换；打印开始自动切到监控页，打印结束回到主界面。

**主界面**
- 时钟、日历（含星期）
- 7 天天气预报（Open-Meteo，免费免 key）：天气图标、降水概率、中文说明、高低温

**打印监控**
- 任务名（过长自动折两行）、进度条、百分比、中文状态词
- 层数、喷嘴 / 热床的当前与目标温度
- 剩余时间、预计结束时间
- 4 台 AMS 槽位：耗材型号与余量、AMS 温湿度

**配置 / 配网**
- STA 信息展示（SSID / IP / 城市 / 经纬度）
- 断网时长按 IO0 进入 AP 网页配网（captive portal，填写 WiFi 与城市）

## 硬件

| 部件 | 型号 / 接线 |
|---|---|
| 主控 | ESP32（Dev Module） |
| 屏幕 | 4.2" 400x300 黑白 e-paper（SSD1619A） |
| SPI | SCK=13 MOSI=14 CS=15 RST=26 DC=27 BUSY=25 |
| 按键 | IO0（切界面 / 长按配网） |

## 快速开始

1. 复制 `config.example.h` 为 `config.h`，填入 WiFi 名称/密码与 Home Assistant 长期令牌（`config.h` 不入库）
2. 编译烧录（见下）
3. 可选：进配置界面用网页改城市与网络

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
arduino-cli upload -p <COM> --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
```

依赖库：GxEPD2、U8g2_for_Adafruit_GFX、Adafruit GFX、ArduinoJson、NTPClient。
