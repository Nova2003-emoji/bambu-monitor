// serial_control.h — 串口控制
// 命令: 0=主界面 1=打印机 2=配置 r=重启(全刷) h=帮助
//       cfg key=value [key=value...] 保存配置到 NVS（Web Serial 网页配网用）
// 待存 key: ssid / pass / token(HA) / city / lat / lon — 保存后发 r 重启生效
#pragma once
#include <Arduino.h>
#include "config_module.h"   // cfgSaveWifi/cfgSaveToken/cfgSaveCity/cfgLat/cfgLon
#include "common.h"          // NL_BSN

static void (*serialCmdHook)(char) = nullptr;   // 额外命令回调（应用层设置）
extern void appSwitchManual(uint8_t runMode);   // 主文件定义（手动切换）

// 解析 "cfg key=value key=value ..."（空格分隔）并写入 NVS
static void serialCfg(const char* line) {
  String ssid, pass, token, city, lat, lon;
  const char* p = line + 3;   // 跳过 "cfg"
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    const char* eq = strchr(p, '=');
    if (!eq) break;
    String key(p, eq - p);
    const char* v0 = eq + 1;
    const char* sp = strchr(v0, ' ');
    String val = sp ? String(v0, sp - v0) : String(v0);
    if      (key == "ssid")  ssid  = val;
    else if (key == "pass")  pass  = val;
    else if (key == "token") token = val;
    else if (key == "city")  city  = val;
    else if (key == "lat")   lat   = val;
    else if (key == "lon")   lon   = val;
    else Serial.printf("[CFG] unknown key: %s" NL_BSN, key.c_str());
    p = sp ? sp : p + strlen(p);
  }
  int n = 0;
  if (ssid.length())  { cfgSaveWifi(ssid.c_str(), pass.c_str()); n++; }
  if (token.length()) { cfgSaveToken(token.c_str());             n++; }
  if (city.length())  { cfgSaveCity(city.c_str(),
                        lat.length() ? lat.c_str() : cfgLat(),
                        lon.length() ? lon.c_str() : cfgLon());  n++; }
  Serial.printf("[CFG] saved %d item(s). Send r to reboot" NL_BSN, n);
}

static void serialControl() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() == 0) continue;
      line.trim();
      char cmd = line[0];
      if (cmd >= '0' && cmd <= '2') {
        uint8_t m = cmd - '0';
        Serial.printf("[SERIAL] switch to mode %d\n", m);
        appSwitchManual(m);
      } else if (cmd == 'r' || cmd == 'R') {
        // 无跨模块全局刷新标志：重启即全刷（墨水屏上电必然全刷）
        Serial.println("[SERIAL] restart for full refresh...");
        delay(300);
        ESP.restart();
      } else if (cmd == 'h' || cmd == 'H') {
        Serial.println("[SERIAL] cmds: 0=main 1=printer 2=config r=restart h=help");
        Serial.println("[SERIAL]   cfg ssid=x pass=y token=z city=c lat=.. lon=..");
        Serial.println("[SERIAL]   cfgget / cfgwipe");
      } else if (line == "cfgget") {
        // 输出当前配置（网页“读取配置”解析 [CFGD] 行填表单）
        Serial.printf("[CFGD] ssid=%s pass=%s token=%s city=%s lat=%s lon=%s" NL_BSN,
          cfgSsid(), cfgPass(), cfgToken(), cfgCity(), cfgLat(), cfgLon());
      } else if (line == "cfgwipe") {
        cfgWipe();
        Serial.println("[CFG] config wiped, defaults restored. Send r to reboot." NL_BSN);
      } else if (line.startsWith("cfg")) {
        serialCfg(line.c_str());
      } else if (serialCmdHook) {
        serialCmdHook(cmd);          // 应用层扩展命令
      } else {
        Serial.printf("[SERIAL] unknown cmd: %c (h for help)\n", cmd);
      }
      line = "";
    } else if (line.length() < 512) {   // 防串口垃圾无限累积
      line += c;
    }
  }
}