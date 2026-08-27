// bambu-monitor.ino — 精简主程序（仅打印机监控模块）
// 框架：功能模块注册表（app_module.h / app_modules.cpp），loop 按 runMode 查表分发。
// 模块：0=主界面 1=打印机监控 2=配置/配网（config_module.cpp）。
// 按键：IO0 短按循环切模块，长按(≥1.5s)强制进配网界面（开热点，与网络状态无关）。
// 网页后端（web_config.cpp）仅进入配置模块时启动，离开即停止。
// 串口：serialControl() 提供 0/1/2 切模块 r=重启 h=帮助（serial_control.h）。

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_task_wdt.h>

// ****** 屏幕（400x300 B/W，SSD1619A）******
GxEPD2_BW<GxEPD2_420_Z96, GxEPD2_420_Z96::HEIGHT> display(
    GxEPD2_420_Z96(/*CS*/ 15, /*DC*/ 27, /*RST*/ 26, /*BUSY*/ 25));

// ****** 公共定义（espWdtFeed/颜色/换行）******
#include "common.h"

// ****** WiFi 凭据（config.h；留空 = 用 NVS 已存凭据）******
#include "config.h"

// ****** 看门狗（setup 注册当前任务；loop 每轮喂见 common.h espWdtFeed）******
static void espWdtInit() {
  esp_task_wdt_config_t cfg = { 10000, 0, true };   // 10s 超时
  esp_err_t e = esp_task_wdt_init(&cfg);
  if (e == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&cfg);  // Arduino 已初始化
  esp_task_wdt_add(NULL);
}

// ****** NTP（printer 模块用 timeClient 走时）******
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp1.aliyun.com", 8 * 3600, 60000);

// ****** 功能模块框架 ******
#include "app_module.h"
#include "web_config.h"   // webCfgStop：离开配置模块时停网页后端
uint8_t g_runMode = 0;                 // 当前模块（0=主界面，1=打印机，2=配置）
static uint32_t g_last_manual_ms = 0;  // 最近手动切换时间戳（冷却期抑制自动切换）
void appSwitchTo(uint8_t runMode) {
  if (g_runMode == 2 && runMode != 2) webCfgStop();   // 离开配置模块 → 停网页后端（仅进配置才启动）
  g_runMode = runMode; Serial.printf("[MAIN] switch to mode %d\n", runMode);
}
// 手动切换（按键）：记录冷却时间戳
void appSwitchManual(uint8_t runMode) {
  g_last_manual_ms = millis();
  appSwitchTo(runMode);
}
// 自动切换（任务驱动）：手动切换后冷却期内抑制
void appSwitchAuto(uint8_t runMode) {
  if (g_runMode == runMode) return;                       // 已在目标模块
  if (millis() - g_last_manual_ms < AUTO_SWITCH_COOLDOWN_MS) {
    Serial.printf("[MAIN] auto-switch suppressed (manual %ds ago)\n",
      (millis() - g_last_manual_ms) / 1000);
    return;
  }
  appSwitchTo(runMode);
}

// ****** 配置（NVS 优先，config.h 为默认值）******
#include "config_module.h"

// ****** 串口控制（r=刷新 h=帮助）******
#include "serial_control.h"

// ****** WiFi 连接（STA；NVS 凭据优先，config.h 为默认）******
char sta_ssid[32] = STA_SSID;
char sta_password[64] = STA_PASSWORD;
void connectToWifi() {
  cfgLoad();                            // NVS 配置载入（含 WiFi 凭据/城市）
  if (cfgSsid()[0] != 0) {
    snprintf(sta_ssid, sizeof(sta_ssid), "%s", cfgSsid());
    snprintf(sta_password, sizeof(sta_password), "%s", cfgPass());
  }
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(WIFI_PS_MIN_MODEM);   // 常驻不深睡，用 modem-sleep 空闲时关射频省电（15s 轮询足够）
  WiFi.mode(WIFI_STA);
  if (sta_ssid[0] != 0) WiFi.begin(sta_ssid, sta_password);
  else                  WiFi.begin();              // NVS 已存凭据（上次配网）
}

void setup() {
  Serial.setRxBufferSize(1024);   // 长配置行（网页 cfg 命令 ~200B）在 loop 阻塞期不丢字节
  Serial.begin(115200);
  delay(200);
  espWdtInit();  // 注册当前任务到 TWDT（loop 每轮喂）

  // 屏幕初始化
  SPI.begin(13, -1, 14, -1);
  display.epd2.selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  display.init(0, 0, 10, 0);
  display.setRotation(0);  // 横屏 400x300

  // 尝试连 WiFi（模块进入时也会确保；连不上时用 IO0 长按进配网界面开热点）
  connectToWifi();

  // IO0 按键（BOOT 键，板载上拉）：短按切模块，长按进配网
  pinMode(0, INPUT_PULLUP);
  Serial.println("[MAIN] setup done, runMode=0 main");
}

void loop() {
  espWdtFeed();
  serialControl();

  // IO0 按键（BOOT，INPUT_PULLUP，按下=LOW）：短按切模块(0→1→2→0)，长按(≥1.5s)强制进配网界面
  static bool key_prev = HIGH;
  static uint32_t key_down_ms = 0, key_deb_ms = 0;
  static bool long_fired = false;
  bool key_now = digitalRead(0);
  if (key_now != key_prev && millis() - key_deb_ms > 30) {   // 电平变化 + 防抖
    key_prev = key_now;
    key_deb_ms = millis();
    if (key_now == LOW) {                 // 按下
      key_down_ms = millis();
      long_fired = false;
    } else {                              // 释放：短按判定
      uint32_t held = millis() - key_down_ms;
      if (held > 50 && held < 1500 && !long_fired) {
        uint8_t next = (g_runMode + 1) % 3;   // 0/1/2 循环
        Serial.printf("[KEY] short %dms -> mode %d\n", held, next);
        appSwitchManual(next);
      }
    }
  } else if (key_now == LOW && !long_fired && millis() - key_down_ms > 1500) {
    long_fired = true;                    // 长按持续 1.5s → 强制进配网界面（开热点，与网络状态无关）
    Serial.println("[KEY] long -> config AP");
    configEnterForceAp();                 // 标记：进配置模块时强制 AP 配网
    appSwitchManual(2);
  }

  // 网页后端只在配置模块内启动（config_enter 调 webCfgBegin），离开时 webCfgStop 停止；
  // 断网不再自动开热点，需要配网时用长按（IO0）进入配网界面。

  // 模块分发：按 g_runMode 查表（0=主界面，1=打印机，2=配置）
  const AppModule* mod = appModuleByRunMode(g_runMode);
  if (mod) {
    static uint8_t lastMode = 0xFF;
    if (mod->enter && lastMode != g_runMode) {   // 切换时重新 enter（模块内做全刷）
      mod->enter();
      lastMode = g_runMode;
    }
    if (mod->loop) mod->loop();
  }
}
