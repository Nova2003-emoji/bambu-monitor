// esp32_compat.h — ESP8266 → ESP32 兼容层（weather-ink 移植用）
// 覆盖：RTC 用户内存、看门狗、LittleFS Dir、FSInfo、MIME、低压检测绕过
#pragma once
#include <Arduino.h>
#include <string.h>
#include <esp_task_wdt.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>

// --- RTC 用户内存（原 ESP8266 ESP.rtcUserMemoryWrite/Read）→ RTC_DATA_ATTR 缓冲 ---
// 注意: addr 是 uint32 字序号（ESP8266 语义），内部 ×4 转字节偏移
#define RTC_MEM_SIZE 256   // 字节；256/4 = 64 个 uint32 序号（weather-ink 用到 16）
RTC_DATA_ATTR uint8_t rtc_user_mem[RTC_MEM_SIZE] = {0};
static inline void rtcUserMemoryWrite(uint32_t addr, const void *data, size_t len) {
  uint32_t off = addr * 4;
  if (off + len <= RTC_MEM_SIZE) memcpy(rtc_user_mem + off, data, len);
}
static inline void rtcUserMemoryRead(uint32_t addr, void *data, size_t len) {
  uint32_t off = addr * 4;
  if (off + len <= RTC_MEM_SIZE) memcpy(data, rtc_user_mem + off, len);
  else memset(data, 0, len);
}

// --- 看门狗（原 ESP.wdtFeed / ESP.wdtEnable / ESP.wdtDisable）---
static inline void espWdtFeed() { esp_task_wdt_reset(); }
static inline void espWdtEnable(uint32_t ms) {
  esp_task_wdt_config_t cfg = { ms, 0, true };
  esp_err_t e = esp_task_wdt_init(&cfg);
  if (e == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&cfg); // Arduino 已初始化 TWDT
  esp_task_wdt_add(NULL);
}
static inline void espWdtDisable() { esp_task_wdt_delete(NULL); }

// --- 文件系统信息（ESP32 core 3.x 移除了 FSInfo；补齐同名字段结构）---
struct FSInfo {
  size_t totalBytes;
  size_t usedBytes;
  size_t blockSize;
  size_t pageSize;
  size_t maxOpenFiles;
  size_t maxPathLength;
};
static inline bool littleFSInfoShim(FSInfo &p) {
  p.totalBytes = LittleFS.totalBytes();
  p.usedBytes = LittleFS.usedBytes();
  return true;
}
static inline bool sdInfoShim(FSInfo &p) {
  p.totalBytes = SD.totalBytes();
  p.usedBytes = SD.usedBytes();
  return true;
}

// --- LittleFS 目录遍历（原 ESP8266 LittleFS.openDir 的 Dir 对象）---
class Dir {
public:
  File _d;   // 打开的目录
  File _f;   // 当前条目
  bool next() { _f = _d.openNextFile(); return (bool)_f; }
  String fileName() { return _f.name(); }
  size_t fileSize() { return _f.size(); }
  bool isDirectory() { return _f.isDirectory(); }
};
static inline Dir openDirCompat(const String &path) {
  Dir d;
  String p = path;
  if (p.length() == 0) p = "/";
  if (p[0] != '/') p = String("/") + p;
  d._d = LittleFS.open(p.c_str());
  return d;
}

// --- MIME（原 ESP8266WebServer 的 mime::getContentType）---
static inline String getContentTypeShim(const String &path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".txt")) return "text/plain";
  if (path.endsWith(".bmp")) return "image/bmp";
  return "application/octet-stream";
}

// --- 低压检测绕过（测试板没有电池分压电路，A0 悬空读数会误判低压→永久休眠）---
#ifndef SKIP_LOWBAT_CHECK
#define SKIP_LOWBAT_CHECK 1
#endif
