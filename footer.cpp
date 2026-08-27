// footer.cpp — 底部栏实现（主界面/打印机/配置界面共用；分钟变化由各模块局刷触发）
// 字体：中文字库 gb2312（uiDrawTextU8g2，与各界面一致）
#include <Arduino.h>
#include <WiFi.h>

#include "sparse_display.h"   // extern display
#include "text_render.h"      // uiDrawTextU8g2/uiTextWidthU8g2
#include "common.h"           // heise
#include "clock.h"            // clockNowSec/clockSynced
#include "footer.h"

void drawFooter() {
  char buf[32];
  // NTP 贴左
  snprintf(buf, sizeof(buf), "%s", clockSynced() ? "NTP OK" : "SYNC..");
  uiDrawTextU8g2(16, FOOT_Y, buf, heise);
  // 时钟居中
  uint32_t now_sec = clockNowSec();
  int h = (now_sec / 3600) % 24, m = (now_sec / 60) % 60;
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  uiDrawTextU8g2((400 - uiTextWidthU8g2(buf)) / 2, FOOT_Y, buf, heise);
  // WIFI 贴右
  snprintf(buf, sizeof(buf), "WIFI %s", WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
  uiDrawTextU8g2(400 - 16 - uiTextWidthU8g2(buf), FOOT_Y, buf, heise);
}
