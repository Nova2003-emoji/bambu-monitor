// common.h — 各模块共享的基础定义（看门狗、颜色、换行、WiFi STA 确认）
// 消除各模块重复的 espWdtFeed/baise/heise/NL_BSN/ensureWifiSta；displayInit 见 sparse_display.h
#pragma once
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <WiFi.h>

// 看门狗喂狗（loop 每轮调用；static inline 各编译单元独立副本，无 ODR 问题）
static inline void espWdtFeed() { esp_task_wdt_reset(); }

// 墨水屏颜色
#define baise  GxEPD_WHITE  //白色
#define heise  GxEPD_BLACK  //黑色

// 串口日志换行（printf 格式串里用）
#define NL_BSN "\n"

// ---- 主文件全局（bambu-monitor.ino 定义）----
extern char sta_ssid[32];
extern char sta_password[64];
extern void connectToWifi();

// 确保 STA 模式已连 WiFi（配置模块切过 AP 后需切回并重连；各模块 enter 调用）
static inline void ensureWifiSta() {
  if (WiFi.getMode() != WIFI_STA || WiFi.status() != WL_CONNECTED) {
    if (sta_ssid[0] != 0) { WiFi.mode(WIFI_STA); WiFi.begin(sta_ssid, sta_password); }
    else                  connectToWifi();
  }
}
