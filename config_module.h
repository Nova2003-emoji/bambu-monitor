// config_module.h — 配置/配网模块（runMode=2）
// 进入方式：
//   短按循环切到（或串口 2）→ STA 已连：信息展示（IP/SSID/城市+经纬度），网页后端
//      以 STA 模式启动（浏览器访问设备 IP 配置），不打断网络；
//      无网络：开热点配网（唯一能配网的方式）。
//   长按(≥1.5s，configEnterForceAp) → 强制配网界面：开热点 "BambuMonitor" + captive portal。
// 网页后端（webCfgBegin）仅在进入本模块时启动，离开时由主文件 webCfgStop 停止。
// 存储：NVS Preferences（namespace "cfg"）：city/lat/lon/wifi_ssid/wifi_pass
#pragma once

void config_enter();   // 进入：按网络状态/force_ap 开热点或信息展示 + 启动网页后端
void config_loop();    // 每轮：handleClient + 屏幕状态刷新

// 长按 IO0 触发：标记进入时强制 AP 配网（开热点，与当前网络状态无关）
void configEnterForceAp();

// NVS 配置读写（供其他模块读取：main 读城市/经纬度，主文件读 WiFi 凭据）
void cfgLoad();        // 载入 NVS → 全局（含默认值）
void cfgSaveCity(const char* city, const char* lat, const char* lon);
void cfgSaveWifi(const char* ssid, const char* pass);
const char* cfgCity();       // 城市名（中文，显示用）
const char* cfgLat();
const char* cfgLon();
const char* cfgSsid();
const char* cfgPass();

// AP 网关 IP（配网界面 IPAddress 与网页 captive portal 跳转 URL 共用，保持一致）
#define AP_GATEWAY_IP  "192.168.3.3"
