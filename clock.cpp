// clock.cpp — 共享时钟实现（唯一编译单元）
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "clock.h"

extern NTPClient timeClient;   // bambu-monitor.ino 全局

static uint32_t last_ntp_ms = 0;   // 最近一次 NTP 校准的 millis
static uint32_t ntp_base_sec = 0;  // 对应 epoch 秒（+8h 已由 timeClient 处理）
static bool     ntp_synced = false;
static uint8_t  ntp_cnt = 0;

void clockInit() {
  timeClient.begin();
}

bool clockSynced() { return ntp_synced; }

uint32_t clockNowSec() {
  return ntp_base_sec + (millis() - last_ntp_ms) / 1000;
}

// 1s 节拍走时；分钟变化返回 true 并更新 h/m（模块互斥运行，static 节拍共享无碍）
bool clockTick(int& h, int& m) {
  static uint32_t last_ms = 0;
  if (millis() - last_ms < 1000) return false;
  last_ms = millis();
  uint32_t now = clockNowSec();
  int nh = (now / 3600) % 24, nm = (now / 60) % 60;
  if (nh == h && nm == m) return false;
  h = nh; m = nm;
  return true;
}

// NTP：未同步每次调用都 forceUpdate 立即重试（不依赖内部 60s 间隔，尽快校准）；
// 成功后每 4 次调用（约 60s）update 校准一次
void clockSync() {
  if (!ntp_synced) {
    if (timeClient.forceUpdate()) {
      ntp_base_sec = timeClient.getEpochTime();
      last_ntp_ms = millis();
      ntp_synced = true;
      Serial.printf("[CLK] ntp cal %s\n", timeClient.getFormattedTime().c_str());
    }
  } else if (ntp_cnt++ % 4 == 0 && timeClient.update()) {
    ntp_base_sec = timeClient.getEpochTime();
    last_ntp_ms = millis();
    Serial.printf("[CLK] ntp cal %s\n", timeClient.getFormattedTime().c_str());
  }
}
