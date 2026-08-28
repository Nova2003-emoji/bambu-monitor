// config_module.cpp — 配置/配网模块（runMode=2）
// 双入口：
//   config_enter 常规进入 → STA 已连：纯信息展示（不打断网络）+ 网页后端 STA 模式启动；
//                           无网络：开热点配网（唯一能配网的方式）
//   configEnterForceAp（IO0 长按）→ 强制 AP 配网界面：开热点 "BambuMonitor" + captive portal
// 网页后端仅在本模块内运行（webCfgBegin 进入时启动，离开时主文件 webCfgStop 停止）。
// 独立编译单元；配置读写经 cfg* 接口暴露（web_config 与 main 共用）。

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>

#include "sparse_display.h"      // extern display + displayInit
#include "chinese_font.h"        // u8g2Fonts（begin 绑定屏幕）
#include "text_render.h"         // uiDrawTextU8g2/uiTextWidthU8g2（统一中文字库渲染）
#include "app_module.h"
#include "config_module.h"   // AP_GATEWAY_IP
#include "web_config.h"
#include "common.h"              // espWdtFeed/baise/heise/NL_BSN
#include "footer.h"             // drawFooter（底部统一样式）

// ---- 默认值（config.h 宏；NVS 有值优先）----
#include "config.h"
#include "fw_version.h"      // FW_VERSION/FW_BUILT（CI 编译时生成）
#ifndef WEATHER_LAT
#define WEATHER_LAT  "22.54"
#define WEATHER_LON  "114.05"
#define WEATHER_CITY "Shenzhen"
#endif

// ---- 热点参数（仅断网降级配网时用）----
#define AP_SSID   "BambuMonitor"
#define AP_PASS   "33333333"
#define AP_MASK   255, 255, 255, 0   // 网关 IP 见 config_module.h AP_GATEWAY_IP

// ---- 配置存储（NVS Preferences；web_config/main 经 cfg* 读取）----
static Preferences prefs;
static char cfg_city[40] = WEATHER_CITY;
static char cfg_lat[16]  = WEATHER_LAT;
static char cfg_lon[16]  = WEATHER_LON;
static char cfg_ssid[32] = STA_SSID;
static char cfg_pass[64] = STA_PASSWORD;
static char cfg_token[192] = HA_TOKEN;

void cfgLoad() {
  prefs.begin("cfg", true);   // 只读打开
  prefs.getString("city", cfg_city, sizeof(cfg_city));
  prefs.getString("lat",  cfg_lat,  sizeof(cfg_lat));
  prefs.getString("lon",  cfg_lon,  sizeof(cfg_lon));
  prefs.getString("wifi_ssid", cfg_ssid, sizeof(cfg_ssid));
  prefs.getString("wifi_pass", cfg_pass, sizeof(cfg_pass));
  prefs.getString("token", cfg_token, sizeof(cfg_token));
  prefs.end();
  Serial.printf("[CFG] city=%s lat=%s lon=%s ssid=%s" NL_BSN,
    cfg_city, cfg_lat, cfg_lon, cfg_ssid[0] ? cfg_ssid : "(nvs-default)");
}

void cfgSaveCity(const char* city, const char* lat, const char* lon) {
  snprintf(cfg_city, sizeof(cfg_city), "%s", city);
  snprintf(cfg_lat,  sizeof(cfg_lat),  "%s", lat);
  snprintf(cfg_lon,  sizeof(cfg_lon),  "%s", lon);
  prefs.begin("cfg", false);
  prefs.putString("city", cfg_city);
  prefs.putString("lat",  cfg_lat);
  prefs.putString("lon",  cfg_lon);
  prefs.end();
}

void cfgSaveWifi(const char* ssid, const char* pass) {
  snprintf(cfg_ssid, sizeof(cfg_ssid), "%s", ssid);
  snprintf(cfg_pass, sizeof(cfg_pass), "%s", pass);
  prefs.begin("cfg", false);
  prefs.putString("wifi_ssid", cfg_ssid);
  prefs.putString("wifi_pass", cfg_pass);
  prefs.end();
}

void cfgSaveToken(const char* tk) {
  snprintf(cfg_token, sizeof(cfg_token), "%s", tk);
  prefs.begin("cfg", false);
  prefs.putString("token", cfg_token);
  prefs.end();
}

const char* cfgToken() { return cfg_token; }

void cfgWipe() {
  prefs.begin("cfg", false);
  prefs.clear();                     // 清 NVS 全部配置
  prefs.end();
  // 内存恢复为 config.h 默认值
  snprintf(cfg_city,  sizeof(cfg_city),  "%s", WEATHER_CITY);
  snprintf(cfg_lat,   sizeof(cfg_lat),   "%s", WEATHER_LAT);
  snprintf(cfg_lon,   sizeof(cfg_lon),   "%s", WEATHER_LON);
  snprintf(cfg_ssid,  sizeof(cfg_ssid),  "%s", STA_SSID);
  snprintf(cfg_pass,  sizeof(cfg_pass),  "%s", STA_PASSWORD);
  snprintf(cfg_token, sizeof(cfg_token), "%s", HA_TOKEN);
}

const char* cfgCity() { return cfg_city; }
const char* cfgLat()  { return cfg_lat; }
const char* cfgLon()  { return cfg_lon; }
const char* cfgSsid() { return cfg_ssid; }
const char* cfgPass() { return cfg_pass; }

// ---- Captive Portal（仅 AP 配网模式）----
static DNSServer dnsServer;
static bool ap_mode = false;       // 当前是否开热点（配网界面）
static bool force_ap = false;      // IO0 长按：强制进配网界面（一次性触发）

// ---- 屏幕显示 ----
// 配置界面统一用 gb2312 中文字库渲染（uiDrawTextU8g2，与城市行一致：16px 方形字）
// 标签右对齐到列右边界 colEnd（列宽 = 最长标签字符数）
static void cfgTextRight(int16_t y, const char* s, int colEnd) {
  uiDrawTextU8g2(colEnd - uiTextWidthU8g2(s), y, s, heise);
}

static void renderConfigScreen() {
  displayInit();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(baise);
    char buf[64];
    if (ap_mode) {
      // 配网界面（长按/无网络进入）：热点信息 + 城市（名称+经纬度）
      uiDrawTextU8g2(16, 30, "CONFIG (AP MODE)", heise);
      snprintf(buf, sizeof(buf), "AP: %s", AP_SSID);
      uiDrawTextU8g2(16, 65, buf, heise);
      snprintf(buf, sizeof(buf), "IP: %s", WiFi.softAPIP().toString().c_str());
      uiDrawTextU8g2(16, 95, buf, heise);
      snprintf(buf, sizeof(buf), "CITY: %s", cfgCity());
      uiDrawTextU8g2(16, 125, buf, heise);
      snprintf(buf, sizeof(buf), "LAT/LON: %s,%s", cfgLat(), cfgLon());
      uiDrawTextU8g2(16, 153, buf, heise);
      uiDrawTextU8g2(16, 193, "Connect AP, browse to IP", heise);
      uiDrawTextU8g2(16, 223, "then save & restart", heise);
    } else {
      // STA 信息界面：标签列宽按最长标签算，标签右对齐到列右，值从列右 +8px 起
      static const char* labels[] = { "SSID", "IP", "CITY", "LAT", "LON" };
      int maxLen = 0;
      for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
        if ((int)strlen(labels[i]) > maxLen) maxLen = strlen(labels[i]);
      const int colEnd = 16 + maxLen * 8;   // 标签列右边界（最宽标签宽度）
      const int vx = colEnd + 8;            // 值起点（不贴右栏）
      uiDrawTextU8g2(16, 25, "CONFIG INFO", heise);
      cfgTextRight(58, "SSID", colEnd);
      uiDrawTextU8g2(vx, 58, WiFi.SSID().c_str(), heise);
      cfgTextRight(86, "IP", colEnd);
      uiDrawTextU8g2(vx, 86, WiFi.localIP().toString().c_str(), heise);
      cfgTextRight(114, "CITY", colEnd);
      uiDrawTextU8g2(vx, 114, cfgCity(), heise);
      cfgTextRight(142, "LAT", colEnd);
      uiDrawTextU8g2(vx, 142, cfgLat(), heise);
      cfgTextRight(170, "LON", colEnd);
      uiDrawTextU8g2(vx, 170, cfgLon(), heise);
      char fwbuf[48];
      snprintf(fwbuf, sizeof(fwbuf), "FW %s %s", FW_VERSION, FW_BUILT);
      uiDrawTextU8g2(16, 192, fwbuf, heise);
      uiDrawTextU8g2(16, 206, "Browse to IP to configure", heise);
      uiDrawTextU8g2(16, 234, "WiFi stays connected", heise);
    }
    drawFooter();   // 底部统一样式（footer.h）
  } while (display.nextPage());
  Serial.printf("[CFG] screen %s" NL_BSN, ap_mode ? "(AP)" : "(STA)");
}

// ---- 模块入口 ----
static bool cfg_inited = false;

void configEnterForceAp() { force_ap = true; }   // IO0 长按：下次进入强制配网界面

void config_enter() {
  if (!cfg_inited) {
    cfgLoad();
    u8g2Fonts.begin(display);   // 中文渲染实例绑定屏幕（幂等）
    cfg_inited = true;
  }

  bool provision = force_ap || WiFi.status() != WL_CONNECTED;
  force_ap = false;   // 一次性触发

  if (provision) {
    // 配网界面：开热点 + captive portal（长按触发，或断网时进入）
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(AP_GATEWAY_IP), IPAddress(AP_GATEWAY_IP), IPAddress(AP_MASK));
    WiFi.softAP(AP_SSID, AP_PASS);
    dnsServer.start(53, "*", IPAddress(AP_GATEWAY_IP));
    webCfgBegin(false);   // AP 模式 captive portal
    ap_mode = true;
    Serial.printf("[CFG] AP started %s" NL_BSN, AP_SSID);
  } else {
    // STA 信息界面：不碰网络；网页后端以 STA 模式启动（浏览器访问设备 IP）
    ap_mode = false;
    webCfgBegin(true);
    Serial.printf("[CFG] STA info, IP %s" NL_BSN, WiFi.localIP().toString().c_str());
  }
  renderConfigScreen();   // 每次进入重画（清屏防旧内容残留）
}

void config_loop() {
  espWdtFeed();
  if (ap_mode) dnsServer.processNextRequest();   // 仅 AP 模式处理 DNS
  webCfgHandle();
}
