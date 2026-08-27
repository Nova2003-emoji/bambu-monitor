# bambu-monitor — ESP32 墨水屏 Bambu Lab 打印机监控

用 **ESP32 + 4.2" 黑白墨水屏（SSD1619A）** 做的 Bambu Lab 3D 打印机监控器 + 桌面信息面板。
数据源为 **Home Assistant REST API**（官方 Bambu Lab 集成），无云、无第三方 MQTT 依赖。

## 功能界面

按 IO0 按键（或串口 0/1/2）切换 3 个模块：

- **主界面**：时钟 + 日历 + 7 天天气（Open-Meteo，免 key）+ 底部状态栏
- **打印监控**：任务名（过长折两行）/ 进度条 / 大百分比+状态词（中文）/ 层数 /
  喷嘴+热床当前与目标温度 / 剩余与预计结束时间 / 4 台 AMS 槽位与余量（型号取自 HA 设备注册表）
- **配置/配网**：STA 信息展示，或长按进入 AP 配网（captive portal）
- 任务驱动自动切换：打印开始自动切到监控页，结束回主界面

## 硬件

- 主控：ESP32（Dev Module）
- 屏：4.2" 400x300 黑白 e-paper（HINK-E042A13-A0 / GDEY042T81，SSD1619A）
- 接口：HW SPI，SCK=13 / MOSI=14 / CS=15 / RST=26 / DC=27 / BUSY=25
- 按键：IO0（BOOT）

## 目录结构

```
bambu-monitor.ino     主程序：模块框架 + WiFi + 按键
main_module.cpp       主界面（时钟/日历/7天天气）
printer_module.cpp    打印监控（HA REST + 字段局刷）
config_module.cpp     配置/配网（NVS + captive portal）
web_config.cpp        Web 配置后端（仅进配置模块时运行）
sparse_display.h      墨水屏渲染（SparseGFXfont + MDI 图标 + 快刷）
text_render.h/.cpp    文本渲染封装（自动 CJK：gb2312 中文字库）
chinese_font.*        U8g2 gb2312 中文字库（253KB）
clock.*               共享时钟（NTP + 本地走时）
footer.*              底部状态栏（NTP/时钟/WIFI）
WeatherIcons_22.h     MDI 天气图标（已归一化 19px）
```

## 配置（重要：凭据不入库）

凭据（WiFi SSID/密码、Home Assistant 长期令牌）**只放本地 `config.h`**，该文件**永不提交**。
从 `config.example.h` 复制为 `config.h` 并填写：

```c
#define STA_SSID     ""          // WiFi 名（或留空用 NVS）
#define STA_PASSWORD ""          // WiFi 密码
#define HA_TOKEN     ""          // Home Assistant 长期访问令牌（打印机数据源）
#define WEATHER_LAT  "22.54"     // 城市经纬度（Web 配网页可按城市名自动解析）
#define WEATHER_LON  "114.05"
#define WEATHER_CITY "Shenzhen"
```

打印机实体前缀在 `printer_module.cpp` 的 `HA_ENT`（需改成你 HA 里实际实体前缀）。
运行后也可经 Web 配置页（断网时长按 IO0 进 AP，访问 `http://192.168.3.3`）保存到 NVS。

## 构建 / 烧录

用 arduino-cli（Arduino IDE 自带路径）：

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
arduino-cli upload -p <COM>  --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
```

需要库：GxEPD2、U8g2_for_Adafruit_GFX、Adafruit GFX、ArduinoJson、NTPClient。

## 说明

- 刷新：15s 数据节拍 + 字段级局刷（变化才刷）+ 状态/任务变化或每 10 次局刷全刷清残影
- WiFi modem-sleep 省电；HA 拉取合并为单次 `/api/template` 请求
- 本项目为自用/学习，凭据约定见 `config.example.h` 注释
