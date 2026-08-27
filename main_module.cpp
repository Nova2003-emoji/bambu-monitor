// main_module.cpp — 主界面模块（runMode=0，开机默认）
// 时钟：全局 timeClient（+8h）走时 + 1s 节拍；日历：epoch → 年月日 + 星期
// 天气：Open-Meteo（Breezy Weather 同源 FOSS，免 key）15s 拉取 + 字段局刷
// 图标：WeatherIcons_22（MDI 官方字体提取，OFL 许可）
// 切换：打印机活跃(running/pause/prepare) → appSwitchTo(1)
// 独立编译单元，内部符号全 static。

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include "sparse_display.h"      // extern display + drawSparseChar + displayInit
#include "qfont16.h"             // QFont16 + drawQFontString
#include "WeatherIcons_22.h"     // MDI 天气图标（SparseGFXfont）
#include "app_module.h"
#include "printer_module.h"      // printerIsActive()
#include "config_module.h"       // cfgCity/cfgLat/cfgLon（NVS 优先）
#include "text_render.h"         // uiDrawText/uiTextWidth（自动 CJK 渲染）
#include "chinese_font.h"        // 中文字库 + u8g2Fonts（唯一编译单元 chinese_font.cpp）
#include "common.h"              // espWdtFeed/baise/heise/NL_BSN
#include "clock.h"              // clockNowSec/clockSync/clockSynced（共享时钟）
#include "footer.h"             // drawFooter（底部统一样式）

// ---- 主文件全局（sta_ssid/sta_password/connectToWifi 的 extern 见 common.h）----
extern NTPClient timeClient;

// ---- 天气 URL（运行时按 NVS 经纬度拼接；config.h 为默认值）----
static String buildWeatherUrl() {
  String url = "http://api.open-meteo.com/v1/forecast?latitude=";
  url += cfgLat();
  url += "&longitude=";
  url += cfgLon();
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
         "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
         "&timezone=Asia%2FShanghai&forecast_days=7";
  return url;
}

#define US_FETCH_MS 15000

// ---------------- 天气数据 ----------------
#define FC_DAYS 7
struct WxData {
  float temp, feels, humidity;     // 当前
  int code;                        // WMO weather_code
  int codeD[FC_DAYS]; float hi[FC_DAYS], lo[FC_DAYS];  // 7 天预报
  int precip[FC_DAYS];             // 7 天降水概率 %
  bool valid;
};
static WxData wx;
static bool wxValid = false;

// WMO weather_code → 图标码点 + 英文缩写 + 中文说明
struct WxMap { int code; uint32_t icon; const char* abbr; const char* cn; };
static const WxMap wxMap[] = {
  { 0,  0xF0599, "SUN", "晴" },    // 晴
  { 1,  0xF0595, "CLO", "晴" },    // 基本晴
  { 2,  0xF0595, "CLO", "多云" },  // 少云
  { 3,  0xF0590, "CLO", "阴" },    // 阴
  { 45, 0xF0591, "FOG", "雾" },    // 雾
  { 48, 0xF0591, "FOG", "雾" },    // 雾凇
  { 51, 0xF0F33, "DRZ", "小雨" },  // 毛毛雨
  { 53, 0xF0F33, "DRZ", "小雨" },
  { 55, 0xF0F33, "DRZ", "小雨" },
  { 56, 0xF0F34, "FZR", "冻雨" },  // 冻毛毛雨
  { 57, 0xF0F34, "FZR", "冻雨" },
  { 61, 0xF0597, "RAI", "小雨" },  // 小雨
  { 63, 0xF0597, "RAI", "中雨" },  // 中雨
  { 65, 0xF0596, "RAI", "大雨" },  // 大雨
  { 66, 0xF0F34, "FZR", "冻雨" },  // 冻雨
  { 67, 0xF0F34, "FZR", "冻雨" },
  { 71, 0xF0598, "SNO", "小雪" },  // 小雪
  { 73, 0xF0598, "SNO", "小雪" },
  { 75, 0xF0598, "SNO", "大雪" },  // 大雪
  { 77, 0xF0717, "SNO", "雪粒" },  // 雪粒
  { 80, 0xF0F33, "SHW", "阵雨" },  // 阵雨
  { 81, 0xF0F33, "SHW", "阵雨" },
  { 82, 0xF0596, "SHW", "强阵雨" },// 强阵雨
  { 85, 0xF0F34, "SNO", "阵雪" },  // 阵雪
  { 86, 0xF0F34, "SNO", "阵雪" },
  { 95, 0xF0593, "LTG", "雷雨" },  // 雷暴
  { 96, 0xF0593, "LTG", "雷雨" },
  { 99, 0xF0593, "LTG", "雷雨" },
};
static const WxMap* wxLookup(int code) {
  for (unsigned i = 0; i < sizeof(wxMap) / sizeof(wxMap[0]); i++)
    if (wxMap[i].code == code) return &wxMap[i];
  return &wxMap[0];  // 兜底 SUN
}

// ---------------- 天气拉取（Open-Meteo） ----------------
static bool fetchWeather() {
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(buildWeatherUrl());
  int code = http.GET();
  if (code != 200) { Serial.printf("[WX] GET -> %d" NL_BSN, code); http.end(); return false; }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) { Serial.println("[WX] json fail"); return false; }
  wx.temp = doc["current"]["temperature_2m"] | -99.0f;
  wx.feels = doc["current"]["apparent_temperature"] | -99.0f;
  wx.humidity = doc["current"]["relative_humidity_2m"] | -1.0f;
  wx.code = doc["current"]["weather_code"] | 0;
  for (int i = 0; i < FC_DAYS; i++) {
    wx.codeD[i] = doc["daily"]["weather_code"][i] | 0;
    wx.hi[i] = doc["daily"]["temperature_2m_max"][i] | -99.0f;
    wx.lo[i] = doc["daily"]["temperature_2m_min"][i] | -99.0f;
    wx.precip[i] = doc["daily"]["precipitation_probability_max"][i] | -1;
  }
  wxValid = (wx.temp > -50);
  Serial.printf("[WX] %s %.0fC feel %.0f hum %.0f%% code %d hi %.0f/lo %.0f" NL_BSN,
    cfgCity(), wx.temp, wx.feels, wx.humidity, wx.code, wx.hi[0], wx.lo[0]);
  return wxValid;
}

// ---------------- 时间/日历（走时/NTP 见 clock.h 共享模块） ----------------
static int clk_h = 0, clk_m = 0;
static int curY, curMo, curD, curW;   // 年月日 + 星期(0=周日)

// epoch 秒 → 年月日（Howard Hinnant civil_from_days 逆算法）
static void epochToDate(uint32_t sec, int& y, int& mo, int& d) {
  long long z = (long long)(sec / 86400) + 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long yy = (long long)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  mo = (int)(mp + (mp < 10 ? 3 : -9));
  y = (int)(yy + (mo <= 2));
}

static const char* wkCn(int w) {
  static const char* a[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
  return a[w % 7];
}

// ---------------- 布局（400x300） ----------------
// Breezy Weather 结构：顶栏(城市左/日期中/天气右) → 大温度 → 体感/高低温 → 预报(横排7列) → 底部
static const int baseX = 16;
#define ROW_H 26
#define TOP_Y     10     // 顶栏：城市(左) + 日期(中) + 天气(右)
#define TEMP_Y    52     // 大温度 QFont24
#define FEEL_Y    92     // 体感+高低温
#define FC_Y      124    // 预报区顶（横排 7 列：星期/日期/图标/降水概率/说明/高低温）
// 行距按真实墨迹统一 8px（U8g2 基线=y+18；数字墨迹 基线-9..-1、CJK 基线-12..0、图标 19px 居中）
#define FC_DATE_Y (FC_Y + 17)   // 行2 日期 基线 159（墨迹 150..158）
#define FC_DESC_Y  (FC_Y + 78)  // 行5 中文说明 基线 220（墨迹 208..220）
#define FC_TMP_Y   (FC_Y + 95)  // 行6 高低温 基线 237（墨迹 228..236）
#define FC_PRECIP_Y (FC_Y + 59) // 行4 降水概率 基线 201（墨迹 192..200）
#define FC_ICON_CY  (FC_Y + 51) // 行3 图标垂直中心 175（墨迹 166..184，上下空隙 8px）

enum { W_CLK, W_HEAD, W_FC, W_FOOT };
struct WField { int x, y, w, h; char last[160]; };
static WField wfields[4] = {
  { 0,   0,   400, 40,  "" },  // W_CLK  顶栏（城市+日期+天气）
  { 0,   44,  400, 66,  "" },  // W_HEAD 大温度+体感+高低温
  { 0,   120, 400, 144, "" },  // W_FC   预报横排 7 列（星期/日期/图标/说明/高低温/降水概率）
  { 0,   268, 400, 26,  "" },  // W_FOOT 底部
};

// 今天+N 天的 月/日（跨月/跨年用 epoch 推算）
static void dateOffset(int off, int& mo, int& d) {
  uint32_t now_sec = clockNowSec();
  int y;
  epochToDate(now_sec + (uint32_t)off * 86400UL, y, mo, d);
}

// 预报横排 N 列：每列 星期+日期 + 图标 + 高低温（星期/日期整块 U8g2 渲染）
static void drawForecast() {
  char buf[64];
  const int colW = 400 / FC_DAYS;
  for (int i = 0; i < FC_DAYS; i++) {
    int cx = i * colW;                       // 列左起点
    const WxMap* m = wxLookup(wx.codeD[i]);
    // 行1 星期：今天 / 周X（今天星期 + i 推算）
    snprintf(buf, sizeof(buf), "%s", i == 0 ? "今天" : wkCn((curW + i) % 7));
    int tw = uiTextWidthU8g2(buf);
    uiDrawTextU8g2(cx + (colW - tw) / 2, FC_Y, buf, heise);
    // 行2 日期 MM-DD（跨月用 epoch 推算）
    int mo, d; dateOffset(i, mo, d);
    snprintf(buf, sizeof(buf), "%02d-%02d", mo, d);
    tw = uiTextWidthU8g2(buf);
    uiDrawTextU8g2(cx + (colW - tw) / 2, FC_DATE_Y, buf, heise);
    // 行3 图标（按实际墨迹宽高，列内水平+垂直居中）
    drawSparseCharCentered(&WeatherIcons_22, cx, FC_ICON_CY, m->icon, heise, colW);
    // 行4 降水概率 %（图标正下方；无数据 --）
    if (wx.precip[i] >= 0) snprintf(buf, sizeof(buf), "%d%%", wx.precip[i]);
    else                   snprintf(buf, sizeof(buf), "--");
    tw = uiTextWidthU8g2(buf);
    uiDrawTextU8g2(cx + (colW - tw) / 2, FC_PRECIP_Y, buf, heise);
    // 行5 中文说明（U8g2）
    tw = uiTextWidthU8g2(m->cn);
    uiDrawTextU8g2(cx + (colW - tw) / 2, FC_DESC_Y, m->cn, heise);
    // 行6 高低温
    snprintf(buf, sizeof(buf), "%.0f/%.0f", wx.hi[i], wx.lo[i]);
    tw = uiTextWidthU8g2(buf);
    uiDrawTextU8g2(cx + (colW - tw) / 2, FC_TMP_Y, buf, heise);
  }
}

// 顶栏：城市（左）+ 中文星期日期（中）+ 天气缩写/图标（右端贴边）
// （vUpdate W_CLK 与 renderFull 共用）
static void drawTopBar() {
  char buf[64];
  uiDrawTextU8g2(baseX, TOP_Y + 2, cfgCity(), heise);   // 城市（左）
  snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d", wkCn(curW), curY, curMo, curD);
  int tw = uiTextWidthU8g2(buf);
  uiDrawTextU8g2((400 - tw) / 2, TOP_Y + 2, buf, heise);   // 日期居中
  const WxMap* m = wxLookup(wx.code);
  int abw = uiTextWidthU8g2(m->abbr);
  int abX = 400 - baseX - abw;                 // 缩写右端贴边
  uiDrawTextU8g2(abX, TOP_Y + 2, m->abbr, heise);
  drawSparseCharRight(&WeatherIcons_22, abX - 4, TOP_Y + 21, m->icon, heise);  // 上移2px
}

// 头部块：当前天气图标+大温度整体居中；下一行体感/高低温居中
// （vUpdate W_HEAD 与 renderFull 共用）
static void drawHeadBlock() {
  char buf[64];
  const WxMap* m = wxLookup(wx.code);
  snprintf(buf, sizeof(buf), "%.0f°", wx.temp);
  int w = uiTextWidthU8g2(buf);
  int iw = sparseCharWidth(&WeatherIcons_22, m->icon);
  int x0 = (400 - (iw + 10 + w)) / 2;
  drawSparseCharLeft(&WeatherIcons_22, x0, TEMP_Y + 21, m->icon, heise);
  uiDrawTextU8g2(x0 + iw + 10, TEMP_Y, buf, heise);
  snprintf(buf, sizeof(buf), "体感 %.0f°  H %.0f  L %.0f", wx.feels, wx.hi[0], wx.lo[0]);
  int w2 = uiTextWidthU8g2(buf);
  uiDrawTextU8g2((400 - w2) / 2, FEEL_Y, buf, heise);
}

static void vUpdate(int idx, const char* s) {
  WField &f = wfields[idx];
  if (strcmp(f.last, s) == 0) return;
  snprintf(f.last, sizeof(f.last), "%s", s);
  display.setPartialWindow(f.x, f.y, f.w, f.h);
  display.firstPage();
  do {
    display.fillRect(f.x, f.y, f.w, f.h, baise);
    // 绘制回调按 idx 分发
    switch (idx) {
      case W_CLK:
        drawTopBar();     // 顶栏：城市+日期+天气缩写/图标
        break;
      case W_HEAD:
        drawHeadBlock();  // 大温度+体感高低温
        break;
      case W_FC:
        drawForecast();
        break;
      case W_FOOT:
        drawFooter();   // 底部统一渲染（footer.h）
        break;
    }
  } while (display.nextPage());
}

// ---------------- 全刷 ----------------
static void renderFull() {
  displayInit();
  display.setFullWindow();
  uint32_t t0 = millis();
  display.firstPage();
  do {
    display.fillScreen(baise);
    for (int i = 0; i < 4; i++) { snprintf(wfields[i].last, sizeof(wfields[i].last), "%s", ""); }
    // 直接画全部（复用与 vUpdate 相同的绘制函数）
    drawTopBar();       // 顶栏：城市+日期+天气缩写/图标
    drawHeadBlock();    // 大温度+体感高低温
    // 预报横排 7 列
    drawForecast();
    // 底部：统一渲染（footer.h）
    drawFooter();
  } while (display.nextPage());
  Serial.printf("[Main] FULL %lums\n", millis() - t0);
}

// ---------------- 模块入口 ----------------
static bool main_inited = false;
static uint32_t last_fetch_ms = 0;
static bool disp_ready = false;

void main_enter() {
  if (!main_inited) {                   // 初始化只做一次
    u8g2Fonts.begin(display);           // 中文城市渲染
    clockInit();
    main_inited = true;
    Serial.println("[Main] init");
  }
  ensureWifiSta();   // 配置模块切过 AP 后确保切回 STA 并重连
  // 每次进入：强制下次 fetch 全刷（清屏画本模块，防旧内容残留）
  last_fetch_ms = 0;
  disp_ready = false;
}

void main_loop() {
  espWdtFeed();
  if (WiFi.status() != WL_CONNECTED) return;

  // 时钟走时（分钟变化立即局刷顶栏日期 + 底部时钟）
  if (clockTick(clk_h, clk_m)) {
    epochToDate(clockNowSec(), curY, curMo, curD);
    curW = timeClient.getDay();
    if (disp_ready) {
      char cb[64];
      // 日期变化 → 顶栏（日期在 W_CLK 里）
      snprintf(cb, sizeof(cb), "%04d%02d%02d%d|%d", curY, curMo, curD, curW, wx.code);
      vUpdate(W_CLK, cb);
      snprintf(cb, sizeof(cb), "%d|%02d:%02d", clockSynced(), clk_h, clk_m); vUpdate(W_FOOT, cb);
    }
  }

  if (millis() - last_fetch_ms >= US_FETCH_MS) {
    last_fetch_ms = millis();
    clockSync();   // NTP 校准（未同步 forceUpdate，同步后每 4 次 update）

    fetchWeather();   // 失败时保留上次数据（wxValid 由 fetchWeather 内部维护）
    // 每轮同时拉取打印机状态（检测打印任务，不画屏）
    printerPoll();
    if (!disp_ready) { renderFull(); disp_ready = true; }
    else {
      char cb[160];
      snprintf(cb, sizeof(cb), "%.0f|%.0f|%.0f|%d", wx.temp, wx.feels, wx.humidity, wx.code);
      vUpdate(W_HEAD, cb);
      // 顶栏天气图标/缩写变化（W_CLK 含日期，变化串含 code 触发重绘）
      snprintf(cb, sizeof(cb), "%04d%02d%02d%d|%d", curY, curMo, curD, curW, wx.code);
      vUpdate(W_CLK, cb);
      // 7 天预报变化串（code + hi + lo + 降水概率 拼接）
      cb[0] = 0;
      for (int i = 0; i < FC_DAYS; i++) {
        char t[24];
        snprintf(t, sizeof(t), "%d|%.0f|%.0f|%d|", wx.codeD[i], wx.hi[i], wx.lo[i], wx.precip[i]);
        strncat(cb, t, sizeof(cb) - strlen(cb) - 1);
      }
      vUpdate(W_FC, cb);
    }
  }

  // 任务驱动切换：打印机活跃 → 切到打印机界面（printerPoll 已更新状态；手动切换后 5 分钟冷却）
  if (printerIsActive()) appSwitchAuto(1);
}
