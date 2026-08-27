// config.example.h — 配置示例（复制为 config.h 并填写实际值；config.h 不入库）
// 说明：运行时可经 Web 配置页（自动降级热点 192.168.3.3）修改，
//       保存到 NVS（Preferences "cfg"）；此处宏仅作默认值。
#pragma once

// WiFi 凭据默认值（NVS 有值优先；留空 = 用 NVS 存储的凭据）
#define STA_SSID     ""
#define STA_PASSWORD ""

// HA 长期访问令牌（NVS 有值优先；留空 = 用 NVS 存储的凭据）
#define HA_TOKEN     ""

// 天气默认值（NVS 有值优先；Web 页填城市名会自动转经纬度）
#define WEATHER_LAT  "22.54"
#define WEATHER_LON  "114.05"
#define WEATHER_CITY "Shenzhen"

// Bambu 实体前缀（HA 里打印机实体的前缀，形如 sensor.a1_XXXX；本地填实际值）
#define HA_ENT_PREF  "sensor.a1_CHANGEME_"
