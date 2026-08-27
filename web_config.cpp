// web_config.cpp — Web 配置服务（仅进配置/配网界面时启动）
// WebServer 提供配置页（城市名 + WiFi + 经纬度），NVS 存储；config 模块进入时按模式
// （STA/AP）启动，离开时 webCfgStop 停止。配网（AP）模式下 + captive portal 重定向
// （DNS 劫持由 config_module 负责）。路由注册只做一次，stop/begin 循环复用。
// 独立编译单元。

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "web_config.h"
#include "config_module.h"   // cfg* 接口 + cfgLoad
#include "common.h"          // NL_BSN

// ---- WebServer ----
static WebServer server(80);
static bool server_started = false;
static bool routes_registered = false;   // 路由只注册一次（stop/begin 循环复用）
static bool portal_mode = false;   // true = AP 模式（captive portal 重定向）

// 任意路径 → 重定向到配置页（captive portal 核心）
static void handleRedirect() {
  server.sendHeader("Location", "http://" AP_GATEWAY_IP "/", true);
  server.send(302, "text/plain", "");
}

static void handleRoot() {
  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>BambuMonitor 配置</title>"
    "<style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 12px}"
    "input{width:100%;padding:8px;margin:4px 0 12px;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#009688;color:#fff;border:0;font-size:16px}"
    ".cur{background:#f5f5f5;padding:8px;margin:4px 0 12px;border-radius:4px;font-size:13px;color:#555}"
    "</style>"
    "</head><body><h2>BambuMonitor 配置</h2>"
    "<form method='POST' action='/save'>"
    "<label>城市（支持中文，如 深圳/上海）</label>"
    "<input name='city' value='");
  h += cfgCity();
  h += F("'><div class='cur'>当前城市: <b>");
  h += cfgCity();
  h += F("</b> &nbsp;经纬度: <b>");
  h += cfgLat();
  h += F(",");
  h += cfgLon();
  h += F("</b></div><label>WiFi 名称（当前连接: ");
  // STA 模式回显实际连接 SSID，AP 模式回显 NVS 配置值
  h += portal_mode ? String(cfgSsid()) : WiFi.SSID();
  h += F("）</label><input name='ssid' value='");
  h += portal_mode ? String(cfgSsid()) : WiFi.SSID();
  h += F("'><label>WiFi 密码（明文显示，便于确认）</label>"
    "<input type='text' name='pass' value='");
  h += cfgPass();
  h += F("'><button type='submit'>保存并重启</button></form></body></html>");
  server.send(200, "text/html", h);
}

// 城市名 → 经纬度（Open-Meteo Geocoding，免 key）
static bool geocodeCity(const char* city, char* latOut, char* lonOut, char* nameOut) {
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=";
  // URL 编码：中文/特殊字符全部 percent-encode（HTTPClient 不自动处理非 ASCII）
  for (const unsigned char* p = (const unsigned char*)city; *p; p++) {
    unsigned char c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.') url += (char)c;
    else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      url += hex;
    }
  }
  url += "&count=1&countryCode=CN&language=zh";

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  int code = http.GET();
  if (code != 200) { Serial.printf("[WEB] geocode -> %d" NL_BSN, code); http.end(); return false; }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) || doc["error"].as<bool>() || doc["results"].isNull()) {
    Serial.println("[WEB] geocode no result");
    return false;
  }
  double la = doc["results"][0]["latitude"] | 0.0;
  double lo = doc["results"][0]["longitude"] | 0.0;
  const char* nm = doc["results"][0]["name"] | city;
  dtostrf(la, 1, 5, latOut);
  dtostrf(lo, 1, 5, lonOut);
  snprintf(nameOut, 40, "%s", nm);
  Serial.printf("[WEB] geocode %s -> %s,%s (%s)" NL_BSN, city, latOut, lonOut, nameOut);
  return true;
}

static void handleSave() {
  String msg;
  bool okCity = true, okWifi = true;

  if (server.hasArg("city") && server.arg("city").length() > 0) {
    String city = server.arg("city");
    city.trim();
    char lat[16], lon[16], name[40];
    if (geocodeCity(city.c_str(), lat, lon, name)) {
      cfgSaveCity(name, lat, lon);
      msg += "城市已保存: " + String(name) + "<br>";
    } else {
      okCity = false;
      msg += "城市未找到: " + city + "<br>";
    }
  }

  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    ssid.trim();
    if (ssid.length() > 0) {
      cfgSaveWifi(ssid.c_str(), server.arg("pass").c_str());
      msg += "WiFi 已保存: " + ssid + "<br>";
    } else {
      okWifi = false;
      msg += "WiFi 名称不能为空<br>";
    }
  }

  if (okCity && okWifi) {
    String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>保存成功</title>"
      "<style>body{font-family:sans-serif;max-width:420px;margin:40px auto;padding:0 12px;text-align:center}"
      "p{font-size:18px}</style></head><body><h2>保存成功</h2><p>");
    h += msg;
    h += F("</p><p>设备即将重启并连接 WiFi…</p></body></html>");
    server.send(200, "text/html", h);
    delay(800);
    ESP.restart();
  } else {
    server.send(200, "text/plain", msg);
  }
}

static void handleStatus() {
  String j = "{\"city\":\"" + String(cfgCity()) + "\",\"lat\":\"" + String(cfgLat())
           + "\",\"lon\":\"" + String(cfgLon()) + "\",\"ssid\":\"" + String(cfgSsid()) + "\"}";
  server.send(200, "application/json", j);
}

// ---- 公开接口 ----
void webCfgBegin(bool staMode) {
  cfgLoad();
  portal_mode = !staMode;

  if (!routes_registered) {              // 路由只注册一次（stop/begin 循环复用）
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/status", handleStatus);
    routes_registered = true;
  }
  server.onNotFound(handleRedirect);     // STA/AP 未知路径都回配置页（captive portal 核心）
  if (!server_started) {
    server.begin();
    server_started = true;
  }
  Serial.printf("[WEB] server started (%s, IP %s)" NL_BSN,
    staMode ? "STA" : "AP",
    staMode ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str());
}

void webCfgStop() {
  if (!server_started) return;
  server.stop();
  server_started = false;
  portal_mode = false;
  Serial.println("[WEB] server stopped");
}

void webCfgHandle() {
  server.handleClient();
}
