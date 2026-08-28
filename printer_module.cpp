// printer_module.cpp — 3D 打印机监控模块（runMode=1）
// 数据源: Home Assistant REST API（实体前缀见 config.h 的 HA_ENT_PREF）
// 字体: QFont 系列（api usage 屏同款，JetBrainsMono 1-bit）：信息行 QFont16(21px)；
//       大 % 与状态词同为 QFont16 同行；图标 MDI_22（与文字中线对齐）
// 排版: 左栏（x 8..192）：型号+时间 / 任务名 / 进度条（内缩做框）/%+状态词 /
//       Layer / 喷嘴+热床温度 / 剩余+结束时间；统一行距 ROW_H=23。
//       右栏（x 200..392）：4 台 AMS 单列竖排，每台独占一块（3 行）：
//       型号+序号+温湿度 / 槽1+2 / 槽3+4。
// 字段窗口互不重叠（含图标/文字完整区域，局刷不裁切）。
// 框架: 无深睡 loop 节拍 + 字段级局刷（变化才刷，每 10 次全刷防残影）；
//       时钟 1s 独立走时（分钟变化立即局刷，不依赖 15s 拉取节拍）。
// 说明: 数据仍走 HA REST；目标温度 = nozzle/bed_target_temperature 实体，
//       结束时间 = HA end_time 实体（ISO8601 UTC → 东八区 HH:MM）；
//       AMS 型号取自 HA 设备注册表（模板 API device_attr → "AMS 2 Pro"）。
// 依赖主文件全局（extern）：timeClient/sta_ssid/sta_password/connectToWifi；
//       display/sparse_display.h、espWdtFeed/common.h 为头文件提供。
// 独立编译单元：本文件所有内部符号 static，与主文件及其他模块互不干扰。

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "sparse_display.h"     // extern display + MDI_22_Sparse 图标 + displayInit
#include "qfont16.h"            // QFont16（含 qfont24.h 的 drawQFontString/qfontWidth）
#include "app_module.h"         // appSwitchTo（任务结束切回主界面）
#include "chinese_font.h"       // hasCjk（ASCII 截断判断）
#include "text_render.h"        // uiDrawText（自动 CJK 渲染）
#include "common.h"             // espWdtFeed/baise/heise/NL_BSN
#include "clock.h"             // clockNowSec/clockSync（共享时钟）
#include "footer.h"            // drawFooter（底部统一样式）
#include "config.h"            // HA_TOKEN 默认值
#include "config_module.h"       // cfgToken（NVS 优先）

// ---- 主文件全局（sta_ssid/sta_password/connectToWifi 的 extern 见 common.h）----
extern NTPClient timeClient;            // bambu-monitor.ino 定义

// HA 地址与实体前缀（token 见 config.h，不再硬编码）
#define HA_HOST   "http://homeassistant.local:8123"
// Bambu 实体前缀：改从 config.h 读（本地真值；config.example.h 为占位，避免序列号入库）

#define US_FETCH_MS    15000   // 拉取/渲染周期（不深睡，loop 常驻节拍）

// ---------------- 数据 ----------------
struct PState {
  int percent; int layer, total;
  float bed, nozzle; float nozzleT, bedT;   // 当前 + 目标温度
  float remainHr;
  char state[20]; char job[70];
  char endTime[24];                          // HA end_time 实体（ISO8601 UTC）
  int amsTemp, amsHumidity;
  char amsModel[16];                         // AMS 型号（HA 设备注册表，模板 API 获取）
  char trayNames[4][20]; int trayRemain[4]; char trayType[4][16]; int trayCount;
};
static PState ps;
static bool psValid = false;

// ---------------- AMS 型号：HA 设备注册表（模板 API） ----------------
// HA 实体 attributes 不含设备型号；型号在设备注册表里，用模板函数
// device_attr(device_id('...'), 'model') 查询（返回如 "AMS 2 Pro"）。
static void fetchAmsModel() {
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(HA_HOST) + "/api/template");
  http.addHeader("Authorization", String("Bearer ") + cfgToken());
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"template\":\"{{ device_attr(device_id('")
      + cfgHaEnt() + "ams_1_temperature'), 'model') }}\"}";
  int code = http.POST(body);
  if (code == 200) {
    String resp = http.getString();
    resp.trim();
    snprintf(ps.amsModel, sizeof(ps.amsModel), "%s", resp.c_str());
    Serial.printf("[Prt] AMS model: %s" NL_BSN, ps.amsModel);
  } else {
    Serial.printf("[HA] template -> %d" NL_BSN, code);
  }
  http.end();
}

// ---------------- 耗材具体型号解析 ----------------
// HA Bambu tray state 名称形如 "Bambu PETG HF"，定位类型词并保留其后的
// 型号词（如 "PETG HF"）。attributes.type 是大类（PETG），state 名含具体型号。
static void parseMaterial(const char* name, char* out, size_t n) {
  static const char* kws[] = {"PLA","PETG","ABS","ASA","TPU","PVA","PET","PCTG","PC","POM","PA"};
  const char* best = nullptr;
  for (unsigned i = 0; i < sizeof(kws) / sizeof(kws[0]); i++) {
    const char* f = strstr(name, kws[i]);
    if (f && (!best || f < best)) best = f;
  }
  if (best) snprintf(out, n, "%s", best);
  else      snprintf(out, n, "%s", name);
  out[n - 1] = 0;
}

// ---------------- 拉取全部实体（单次 /api/template 渲染，替代逐实体 GET） ----------------
// 模板一次性输出 JSON：数值加 |int/-1、|float/0 兜底（实体缺失时 HA 给
// unavailable 文本，过滤器兜成数字保证整包仍是合法 JSON）；字符串用 |to_json
// 自动加引号转义。状态为 unavailable/unknown 时判为无效（等同原 404 → 离线）。
static String jsonEscape(const String& s) {   // 模板嵌入 POST body 的 JSON 字符串转义
  String out;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

// ---------------- 实体前缀自动发现 ----------------
// 从 HA 全量传感器中匹配 *_print_progress 结尾的实体，反推实体前缀
// （如 sensor.a1_03919d552104522_print_progress → sensor.a1_03919d552104522_）。
// NVS 前缀为空/占位时自动触发；多台打印机取第一台（可在网页手动覆盖）。
static bool autoDiscoverEntity() {
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(HA_HOST) + "/api/template");
  http.addHeader("Authorization", String("Bearer ") + cfgToken());
  http.addHeader("Content-Type", "application/json");
  String tmpl = "{{ states.sensor | selectattr('entity_id','search','_print_progress$') "
                "| map(attribute='entity_id') | list | to_json }}";
  int code = http.POST("{\"template\":\"" + jsonEscape(tmpl) + "\"}");
  if (code != 200) { Serial.printf("[HA] discover -> %d" NL_BSN, code); http.end(); return false; }
  String resp = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp)) return false;
  const char* first = doc[0] | "";
  const char* tail = "_print_progress";
  if (!first[0]) return false;
  const char* pos = strstr(first, tail);
  if (!pos || pos == first) return false;   // 无匹配实体
  String prefix = String(first).substring(0, pos - first);
  if (prefix.length() < 5) return false;
  cfgSaveHaEnt(prefix.c_str());
  Serial.printf("[HA] entity prefix auto-discovered: %s" NL_BSN, prefix.c_str());
  return true;
}

static bool fetchPrinter() {
  // 实体前缀自动发现（NVS 为空/占位时自动触发；成功前每轮 fetch 都跳过数据请求）
  static bool discovered = false;        // 生命周期内只发现一次
  const char* E = cfgHaEnt();
  if (!discovered && (!E[0] || strstr(E, "CHANGEME"))) {
    discovered = autoDiscoverEntity();   // 成功后 NVS/内存缓冲已是新前缀
    E = cfgHaEnt();                      // 重读（发现成功则为新前缀）
    if (!discovered) {                   // 失败：本轮跳过数据请求（避免占位模板刷屏），60s 后随下一轮重试
      static uint32_t last_try = 0;
      if (millis() - last_try < 60000) return false;
      last_try = millis();
    }
  } else if (E[0] && !strstr(E, "CHANGEME")) {
    discovered = true;                   // NVS 已有真实前缀
  }
    String tmpl = "{";
  tmpl += "\"p\":{{states('"; tmpl += E; tmpl += "print_progress')|int(-1)}},";
  tmpl += "\"l\":{{states('"; tmpl += E; tmpl += "current_layer')|int(-1)}},";
  tmpl += "\"t\":{{states('"; tmpl += E; tmpl += "total_layer_count')|int(-1)}},";
  tmpl += "\"b\":{{states('"; tmpl += E; tmpl += "bed_temperature')|float(0)}},";
  tmpl += "\"n\":{{states('"; tmpl += E; tmpl += "nozzle_temperature')|float(0)}},";
  tmpl += "\"bt\":{{states('"; tmpl += E; tmpl += "bed_target_temperature')|float(0)}},";
  tmpl += "\"nt\":{{states('"; tmpl += E; tmpl += "nozzle_target_temperature')|float(0)}},";
  tmpl += "\"r\":{{states('"; tmpl += E; tmpl += "remaining_time')|float(0)}},";
  tmpl += "\"s\":{{states('"; tmpl += E; tmpl += "print_status')|to_json}},";
  tmpl += "\"j\":{{states('"; tmpl += E; tmpl += "task_name')|to_json}},";
  tmpl += "\"et\":{{states('"; tmpl += E; tmpl += "end_time')|to_json}},";
  tmpl += "\"at\":{{states('"; tmpl += E; tmpl += "ams_1_temperature')|float(0)}},";
  tmpl += "\"ah\":{{states('"; tmpl += E; tmpl += "ams_1_humidity')|float(0)}},";
  tmpl += "\"trays\":[{%- for i in range(1,5)%}";
  tmpl += "{\"n\":{{states('"; tmpl += E; tmpl += "ams_1_tray_'~i)|to_json}},\"r\":{{state_attr('"; tmpl += E; tmpl += "ams_1_tray_'~i,'remain')|int(-1)}}}";
  tmpl += "{%- if not loop.last%},{%endif%}";
  tmpl += "{%- endfor%}";
  tmpl += "]}";

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(HA_HOST) + "/api/template");
  http.addHeader("Authorization", String("Bearer ") + cfgToken());
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"template\":\"" + jsonEscape(tmpl) + "\"}");
  String resp = (code == 200) ? http.getString() : String("");
  if (code != 200) {
    http.end();
    return false;
  }
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { Serial.println("[HA] json fail"); return false; }

  bool ok = true;
  ps.percent = doc["p"] | -1;  if (ps.percent < 0) ok = false;
  ps.layer   = doc["l"] | -1;  if (ps.layer   < 0) ok = false;
  ps.total   = doc["t"] | -1;  if (ps.total   < 0) ok = false;
  ps.bed     = doc["b"] | 0.0f;
  ps.nozzle  = doc["n"] | 0.0f;
  ps.bedT    = doc["bt"] | 0.0f;
  ps.nozzleT = doc["nt"] | 0.0f;
  ps.remainHr = doc["r"] | 0.0f;
  const char* st = doc["s"] | "";
  snprintf(ps.state, sizeof(ps.state), "%s", st);
  if (strcmp(st, "unavailable") == 0 || strcmp(st, "unknown") == 0) ok = false;
  snprintf(ps.job, sizeof(ps.job), "%s", (const char*)(doc["j"] | ""));
  snprintf(ps.endTime, sizeof(ps.endTime), "%s", (const char*)(doc["et"] | ""));
  ps.amsTemp     = (int)(doc["at"] | 0.0f);
  ps.amsHumidity = (int)(doc["ah"] | 0.0f);
  // 料盘（最多 4 个）：state=名称（含具体型号），attributes.remain=余量%；
  // 实体不存在时 HA 返回 "unavailable"/"unknown" → 视为无此槽位（等同原先 404 break）
  ps.trayCount = 0;
  for (JsonVariant t : doc["trays"].as<JsonArray>()) {
    if (ps.trayCount >= 4) break;
    const char* nm = t["n"] | "";
    if (nm[0] == 0 || strcmp(nm, "unavailable") == 0 || strcmp(nm, "unknown") == 0) break;
    snprintf(ps.trayNames[ps.trayCount], 20, "%s", nm);
    parseMaterial(nm, ps.trayType[ps.trayCount], 16);
    JsonVariant rv = t["r"];
    ps.trayRemain[ps.trayCount] = rv.is<int>() ? rv.as<int>() : -1;
    ps.trayCount++;
  }
  psValid = ok;
  Serial.printf("[Prt] %s %d%% L%d/%d nz=%.1f/%.1f bed=%.1f/%.1f remain=%.1fh AMS=%dC/%d%%" NL_BSN,
    ps.state, ps.percent, ps.layer, ps.total, ps.nozzle, ps.nozzleT, ps.bed, ps.bedT, ps.remainHr,
    ps.amsTemp, ps.amsHumidity);
  for (int i = 0; i < ps.trayCount; i++)
    Serial.printf("  [Tray%d] %s remain=%d" NL_BSN, i + 1, ps.trayType[i], ps.trayRemain[i]);
  return ok;
}

// ---------------- 状态显示（HA 原始状态 → 中文） ----------------
static const char* stateLabel() {
  if (!psValid) return "离线";
  if (strcmp(ps.state, "running") == 0) return "打印中";
  if (strcmp(ps.state, "finish") == 0)  return "完成";
  if (strcmp(ps.state, "idle") == 0)    return "空闲";
  if (strcmp(ps.state, "pause") == 0)   return "暂停";
  if (strcmp(ps.state, "prepare") == 0) return "准备中";
  if (strcmp(ps.state, "slicing") == 0 || strcmp(ps.state, "sliced") == 0) return "切片中";
  if (strcmp(ps.state, "failed") == 0)  return "失败";
  if (strcmp(ps.state, "offline") == 0) return "离线";
  return ps.state;   // 未知状态保留原始值
}

// ---------------- 剩余时间格式化（HA 单位小时 → "HH:MM"，与 ETA 格式一致） ----------------
static void fmtRemain(char* b, size_t n) {
  if (!psValid || ps.remainHr <= 0) { snprintf(b, n, "--:--"); return; }
  int totalMin = (int)(ps.remainHr * 60 + 0.5);
  snprintf(b, n, "%02d:%02d", totalMin / 60, totalMin % 60);
}

// 儒略日 → 天数（Howard Hinnant days_from_civil）
static long long daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  long long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}

// HA end_time "YYYY-MM-DDTHH:MM:SS+00:00"（UTC）→ 本地 HH:MM（东八区 +8）
static void fmtEta(char* b, size_t n) {
  int y, mo, d, h, mi;
  if (ps.endTime[0] == 0 || sscanf(ps.endTime, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) {
    snprintf(b, n, "--");
    return;
  }
  long long local = (daysFromCivil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL) + 8 * 3600LL;
  long long eh = (local / 3600) % 24, em = (local / 60) % 60;
  snprintf(b, n, "%02lld:%02lld", eh, em);
}

// ---------------- 状态（RAM，不深睡不丢） ----------------
static bool prt_inited = false;
static uint32_t last_fetch_ms = 0;
static int prt_h = 0, prt_m = 0;       // 显示用时间（走时/NTP 见 clock.h）

// ---------------- 布局（400x300 横屏） ----------------
// 统一行距 ROW_H=23（行顶间距固定）。大 % 与状态词 QFont16 同行。
// 图标(22px)与文字(21px)中线对齐（图标基线 = 文字顶 + 21）。
// 字段窗口互不重叠且覆盖各自文字/图标完整区域（局刷不裁切）。
static const int baseX = 8;
#define ROW_H  23
// 左上角 Bambu logo（35x46 两行高，图片位图），型号放图标右边
#define LOGO_X   8
#define LOGO_Y   8
#define LOGO_W   35
#define LOGO_H   46
#define TITLE_X  (LOGO_X + LOGO_W + 8)   // 型号（图标右侧）
#define TITLE_Y  (LOGO_Y + (LOGO_H - 21) / 2)  // 垂直居中
#define TASK_Y   (LOGO_Y + LOGO_H + 2)   // 任务名行1（紧贴 logo 底部）
#define TASK2_Y  (TASK_Y + 20)           // 任务名行2（长名折行）
#define PROG_X   8             // 行3 进度条（内缩做框）
#define PROG_W   150
#define PROG_Y   100
#define PROG_H   12
#define PCT_Y    118           // 行4 大 % + 状态词
#define LAY_Y    141           // 行5 层数
#define TEMP_Y   167           // 行6 喷嘴温度（图标基线 TEMP_Y+21）
#define BED_Y    195           // 行7 热床温度（与喷嘴行距 28 加大，图标基线 BED_Y+21）
#define REM_Y    222           // 行8 剩余+结束时间（预计 = REM_Y+ROW_H=245，墨迹底 263 不盖 footer 268）
#define AMS_X      200
#define AMS_COL_W  192        // 右栏宽度（200..392）
#define AMS_TITLE_Y0 6        // 右栏顶部（上移 4px）
#define AMS_ROW_H   22        // 右栏行距（比左栏 23 缩小，避免 AMS4 盖住时钟）
#define AMS_PAIR_DY (3 * AMS_ROW_H)  // 每台 AMS 3 行（标题/槽1+2/槽3+4）= 66

enum { F_JOB, F_PROG, F_LAY, F_TEMP, F_BED, F_REM, F_AMS, F_CLK };
// 任务名折两行后整块窗口高 44（行1+行2）；截断交给 drawJob 按像素宽度处理
#define JOB_LINE_W (fields[F_JOB].w - 2)   // 任务名每行可用宽度
// 字段窗口（x, y, w, h）+ 变化检测（互不重叠，贴合字段本身）
struct PField { int x, y, w, h; char last[64]; };
static PField fields[8] = {
  { baseX, TASK_Y - 2,  170, 44,  "" },  // F_JOB  任务名（两行）
  { baseX, 98,  184, 42,  "" },  // F_PROG 进度条+大%+状态词
  { baseX, 139, 110, 23,  "" },  // F_LAY  层数 x/y
  { baseX, 164, 130, 25,  "" },  // F_TEMP 喷嘴温度（含图标）
  { baseX, 192, 130, 27,  "" },  // F_BED  热床温度（含图标）
  { baseX, 220, 130, 45,  "" },  // F_REM  剩余时间两行（剩余 + 预计）
  { AMS_X, 6,   192, 264, "" },  // F_AMS  4 台 AMS 单列（每台 3 行）
  { 0,   268, 400, 26,  "" },  // F_CLK  底部栏（NTP/时钟/WIFI，footer.h）
};

// Bambu Lab（拓竹）logo：截图转位图（35x46），LSB-first（适配 drawXBitmap）
static const uint8_t bambuLogoBits[] PROGMEM = {
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x3F,0x00,0x00,
  0x00,0x00,0xFF,0x00,0x00,
  0x00,0x00,0xF7,0x07,0x00,
  0x00,0x00,0xC7,0x1F,0x00,
  0x00,0x00,0x07,0xFE,0x00,
  0x00,0x00,0x07,0xF8,0x03,
  0x00,0x00,0x07,0xC0,0x07,
  0x00,0x00,0x07,0x00,0x07,
  0x00,0xE0,0x07,0x00,0x00,
  0x00,0xF8,0x07,0x00,0x00,
  0x00,0x7F,0x07,0x00,0x00,
  0xC0,0x0F,0x07,0x00,0x00,
  0xF8,0x03,0x07,0x00,0x00,
  0x7E,0x00,0x07,0x00,0x00,
  0x1F,0x00,0x07,0x00,0x00,
  0x03,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
  0x00,0x00,0x07,0x00,0x00,
};
static void drawBambuLogo(int16_t x, int16_t y) {
  display.fillRect(x, y, LOGO_W, LOGO_H, heise);              // 黑背景
  display.drawXBitmap(x, y, bambuLogoBits, LOGO_W, LOGO_H, baise);
}

// 单台 AMS 块：标题(型号+序号+温湿度 右对齐) / 槽1+2 / 槽3+4（无余量预估）
// idx = AMS 序号（0..3）；仅 idx==0 有 AMS1 数据，其余温湿度/槽位占位 "--"
static void drawAms(int x, int y, int idx) {
  char buf[64];
  // 标题：型号+序号（左对齐）+ 温湿度（右对齐列尾；AMS2-4 占位 "--" 位置一致）
  snprintf(buf, sizeof(buf), "%s %d", ps.amsModel, idx + 1);
  uiDrawTextU8g2(x, y, buf, heise);
  if (idx == 0)
    snprintf(buf, sizeof(buf), "%3d°C %d%%", ps.amsTemp, ps.amsHumidity);
  else
    snprintf(buf, sizeof(buf), "--");
  int rw = uiTextWidthU8g2(buf);
  uiDrawTextU8g2(x + AMS_COL_W - rw, y, buf, heise);
  // 槽 2 行，每行 2 槽（1 2 / 3 4），固定槽宽 96，无余量
  for (int pair = 0; pair < 2; pair++) {
    int y2 = y + AMS_ROW_H + pair * AMS_ROW_H;
    for (int k = 0; k < 2; k++) {
      int t = pair * 2 + k;
      int sx = x + k * 96;
      if (idx == 0 && t < ps.trayCount) {
        char typeFirst[8];
        sscanf(ps.trayType[t], "%7s", typeFirst);   // 宽度限 7，防长型号词溢出
        snprintf(buf, sizeof(buf), "%d %s", t + 1, typeFirst);
      } else {
        snprintf(buf, sizeof(buf), "%d --", t + 1);
      }
      uiDrawTextU8g2(sx, y2, buf, heise);
    }
  }
}

// 任务名绘制：一行放得下直接画；过长按 UTF-8 字符边界折两行
// （切点取前缀宽度最接近总宽一半处；超过两行容量时丢尾部）
static void drawJob(int x, int y) {
  char buf[70];
  snprintf(buf, sizeof(buf), "%s", psValid ? ps.job : "---");
  int total = uiTextWidthU8g2(buf);
  if (total <= JOB_LINE_W) {                 // 一行放得下
    uiDrawTextU8g2(x, y, buf, heise);
    return;
  }
  while (total > 2 * JOB_LINE_W && buf[0]) { // 保两行各不超宽：从尾按字符丢弃
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] & 0xC0) == 0x80) len--;   // 去续字节
    if (len > 0) len--;
    buf[len] = 0;
    total = uiTextWidthU8g2(buf);
  }
  int len = strlen(buf);
  int best = -1, bestdiff = total;
  for (int i = 1; i < len; i++) {
    if ((buf[i] & 0xC0) == 0x80) continue;   // 跳过 UTF-8 续字节
    char save = buf[i]; buf[i] = 0;
    int diff = abs(uiTextWidthU8g2(buf) - total / 2);
    buf[i] = save;
    if (diff < bestdiff) { bestdiff = diff; best = i; }
  }
  if (best < 0) best = len;
  char save = buf[best]; buf[best] = 0;
  uiDrawTextU8g2(x, y, buf, heise);              // 行1
  uiDrawTextU8g2(x, TASK2_Y, buf + best, heise); // 行2
}

// 画字段内容（窗口内）
static void drawField(int idx) {
  char buf[64];
  switch (idx) {
    case F_JOB:
      drawJob(baseX, TASK_Y);
      break;
    case F_PROG: {
      // 行3 进度条：外框 + 填充内缩 2px 做框（框线清晰可见）
      int py = PROG_Y + (ROW_H - PROG_H) / 2;       // 行内垂直居中
      display.drawRect(PROG_X, py, PROG_W, PROG_H, heise);
      int fw = PROG_W * ps.percent / 100;
      if (fw >= 4) display.fillRect(PROG_X + 2, py + 2, fw - 4, PROG_H - 4, heise);
      // 行4 大 % + 状态词（QFont16，% 后跟状态词）
      snprintf(buf, sizeof(buf), "%d%%", ps.percent);
      int w = uiTextWidthU8g2(buf);
      uiDrawTextU8g2(baseX, PCT_Y, buf, heise);
      // 状态词右对齐进度条右端（PROG_X+PROG_W），中文走 U8g2
      int sw = uiTextWidthU8g2(stateLabel());
      uiDrawTextU8g2(PROG_X + PROG_W - sw, PCT_Y, stateLabel(), heise);
      break;
    }
    case F_LAY: {
      // 行5 层数：整行（标签+数值）合成一个字符串，统一中文字库渲染
      if (ps.total > 0) snprintf(buf, sizeof(buf), "层数 %d/%d", ps.layer, ps.total);
      else              snprintf(buf, sizeof(buf), "层数 --");
      uiDrawTextU8g2(baseX, LAY_Y, buf, heise);
      break;
    }
    case F_TEMP: {
      // 行6 喷嘴温度：图标 + 当前 目标（目标无数据 "--" 居中于 3 字符槽）
      char t[16];
      snprintf(t, sizeof(t), "%3.0f°C", ps.nozzle);
      uiDrawTextU8g2(42, TEMP_Y, t, heise);
      int tx = 42 + uiTextWidthU8g2(t) + 16;        // 当前值 + 2 空格
      if (ps.nozzleT > 0) {
        snprintf(t, sizeof(t), "%3.0f°C", ps.nozzleT);
        uiDrawTextU8g2(tx, TEMP_Y, t, heise);
      } else {
        uiDrawTextU8g2(tx + 4, TEMP_Y, "--", heise);  // 槽 24px，-- 16px，居中 +4
      }
      drawSparseChar(&MDI_22_Sparse, 8, TEMP_Y + 21, MDI_NOZZLE, heise);
      break;
    }
    case F_BED: {
      // 行7 热床温度：文字下移 2px、与图标间距拉大
      char t[16];
      snprintf(t, sizeof(t), "%3.0f°C", ps.bed);
      uiDrawTextU8g2(42, BED_Y + 2, t, heise);
      int tx = 42 + uiTextWidthU8g2(t) + 16;        // 当前值 + 2 空格
      if (ps.bedT > 0) {
        snprintf(t, sizeof(t), "%3.0f°C", ps.bedT);
        uiDrawTextU8g2(tx, BED_Y + 2, t, heise);
      } else {
        uiDrawTextU8g2(tx + 4, BED_Y + 2, "--", heise);
      }
      drawSparseChar(&MDI_22_Sparse, 8, BED_Y + 21, MDI_BED, heise);
      break;
    }
    case F_REM: {
      // 行8/9 剩余时间两行：剩余（行1）+ 预计（行2），时钟图标垂直居中
      int iconBase = REM_Y + ROW_H + 8;          // 两行中心 + 图标半高（上移 2px）
      drawSparseChar(&MDI_22_Sparse, baseX, iconBase, MDI_CLOCK, heise);
      char b2[24], b3[8];
      fmtRemain(b2, sizeof(b2));
      // 标签中文走 U8g2（16px），时间值同字体；标签+8px 间距后接时间
      static const char* L_REM = "剩余";
      static const char* L_ETA = "预计";
      int lw = uiTextWidthU8g2(L_REM);
      uiDrawTextU8g2(46, REM_Y, L_REM, heise);              // 右移 4px（相对上一版 42）
      uiDrawTextU8g2(46 + lw + 8, REM_Y, b2, heise);
      if (psValid && ps.remainHr > 0) {
        fmtEta(b3, sizeof(b3));
      } else {
        snprintf(b3, sizeof(b3), "--:--");     // 与 剩余 占位符一致
      }
      uiDrawTextU8g2(46, REM_Y + ROW_H, L_ETA, heise);
      uiDrawTextU8g2(46 + lw + 8, REM_Y + ROW_H, b3, heise);
      break;
    }
    case F_AMS: {
      // 4 台 AMS 单列竖排，每台独占一块（drawAms 调用 4 次，排版一致）
      for (int a = 0; a < 4; a++)
        drawAms(AMS_X, AMS_TITLE_Y0 + a * AMS_PAIR_DY, a);
      break;
    }
    case F_CLK:
      drawFooter();   // 底部统一渲染（footer.h）
      break;
  }
}

// 字段 key：局刷变化检测 / 局刷入参 / renderFull 后 last 同步三处共用（格式唯一来源）
// 注意：key 只用于比较与 vUpdate 入参，绘制内容由 drawField 决定。
static void fieldKey(int idx, char* buf, size_t n) {
  switch (idx) {
    case F_JOB:
      snprintf(buf, n, "%s", ps.job);   // 完整任务名作 key（折行/截断由 drawJob 处理）
      break;
    case F_PROG: snprintf(buf, n, "%d|%s", ps.percent, stateLabel()); break;
    case F_LAY:
      if (ps.total > 0) snprintf(buf, n, "%d/%d", ps.layer, ps.total);
      else              snprintf(buf, n, "--");
      break;
    case F_TEMP: snprintf(buf, n, "%.0f|%.0f", ps.nozzle, ps.nozzleT); break;
    case F_BED:  snprintf(buf, n, "%.0f|%.0f", ps.bed, ps.bedT); break;
    case F_REM: {
      char b2[24], b3[8];
      fmtRemain(b2, sizeof(b2));
      if (psValid && ps.remainHr > 0) {
        fmtEta(b3, sizeof(b3));
        snprintf(buf, n, "%s|%s", b2, b3);
      } else {
        snprintf(buf, n, "%s", b2);
      }
      break;
    }
    case F_AMS:  snprintf(buf, n, "%d|%d", ps.amsTemp, ps.amsHumidity); break;
    default:     buf[0] = 0; break;   // F_CLK 由 1s 节拍独立驱动
  }
}

// 字段更新：变化才局刷（清窗 + 重画）
static void vUpdate(int idx, const char* s) {
  PField &f = fields[idx];
  if (strcmp(f.last, s) == 0) return;
  snprintf(f.last, sizeof(f.last), "%s", s);
  display.setPartialWindow(f.x, f.y, f.w, f.h);
  display.firstPage();
  do {
    display.fillRect(f.x, f.y, f.w, f.h, baise);
    drawField(idx);
  } while (display.nextPage());
}

// ---------------- 全刷（首次/状态变化/每 10 次局刷） ----------------
static bool disp_ready = false;   // 首次全刷后才允许局刷（vUpdate 需 display 已初始化）
static void renderFull() {
  displayInit();

  display.setFullWindow();
  display.firstPage();
  do {
    // 左上角 Bambu logo + 型号（图标右侧）+ 全部字段
    drawBambuLogo(LOGO_X, LOGO_Y);
    uiDrawTextU8g2(TITLE_X, TITLE_Y, "A1", heise);
    for (int i = 0; i < 8; i++) drawField(i);
  } while (display.nextPage());

  // 同步全部字段 last 缓存（fieldKey 唯一来源；下一轮无变化则不局刷）
  char tmp[64];
  for (int i = 0; i < 8; i++) {
    fieldKey(i, tmp, sizeof(tmp));
    snprintf(fields[i].last, sizeof(fields[i].last), "%s", tmp);
  }
  disp_ready = true;
  Serial.println("[Prt] FULL");
}

// ---------------- 模块入口 ----------------
void printer_enter() {
  if (!prt_inited) {                    // 初始化只做一次
    u8g2Fonts.begin(display);          // 中文任务名渲染
    clockInit();
    prt_inited = true;
    Serial.println("[Prt] init");
  }
  ensureWifiSta();   // 配置模块切过 AP 后确保切回 STA 并重连
  // 每次进入：强制下次 fetch 全刷（清屏画本模块，防旧内容残留）
  last_fetch_ms = 0;
  disp_ready = false;
}

void printer_loop() {
  espWdtFeed();
  if (WiFi.status() != WL_CONNECTED) {
    // 避免每轮重复 WiFi.begin()（连接中重复调用报 cannot set config）；
    // 仅当长时间未连上时重试（15s 一次）
    static uint32_t last_wifi_retry = 0;
    if (millis() - last_wifi_retry > US_FETCH_MS) {
      last_wifi_retry = millis();
      if (sta_ssid[0] != 0) WiFi.begin(sta_ssid, sta_password);
      else                  WiFi.begin();
      Serial.printf("[Prt] wifi retry, ssid=%s" NL_BSN, sta_ssid[0] ? sta_ssid : "(nvs)");
    }
    return;
  }

  // 时钟独立走时：分钟变化立即局刷（不依赖 15s fetch，减少显示延迟）
  if (disp_ready && clockTick(prt_h, prt_m)) {
    char cb[8];
    snprintf(cb, sizeof(cb), "%02d:%02d", prt_h, prt_m);
    vUpdate(F_CLK, cb);
  }

  if (millis() - last_fetch_ms >= US_FETCH_MS) {
    last_fetch_ms = millis();

    clockSync();   // NTP 校准（未同步 forceUpdate，同步后每 4 次 update）

    // 拉取
    bool ok = fetchPrinter();
    psValid = ok;

    // AMS 型号：首次成功拉取后缓存（设备注册表型号不变）
    static bool amsModelFetched = false;
    if (!amsModelFetched && ok) {
      fetchAmsModel();
      amsModelFetched = true;
    }

    // 状态/任务名变化 → 全刷；否则字段局刷
    static char lastState[20] = "", lastJob[70] = "";
    // 本轮除时钟外是否有字段变化（比较 last 缓存；F_CLK 由 1s 节拍独立更新）
    char buf[64];
    bool anyChanged = false;
    for (int i = F_JOB; i <= F_AMS; i++) {
      fieldKey(i, buf, sizeof(buf));
      if (strcmp(fields[i].last, buf) != 0) anyChanged = true;
    }

    // 整 5 分钟全刷：跨过 5 分钟块边界 且 本轮除时钟外有字段变化才全刷
    // （只有时间在走 → 走局刷分支，vUpdate 比较相同会跳过，不浪费全刷）
    static uint32_t last_full_block = 0;
    bool fiveMinFull = false;
    if (clockSynced() && anyChanged) {
      uint32_t now_sec = clockNowSec();
      uint32_t block = now_sec / 300;                  // 5 分钟块号
      if (block != last_full_block) { last_full_block = block; fiveMinFull = true; }
    }
    // 数据获取失败时不反复全刷（离线/占位实体会每轮失败）——60s 节流一次
    static uint32_t last_fail_full = 0;
    bool failFull = (!ok || !psValid) && (millis() - last_fail_full > 60000);
    if (failFull) last_fail_full = millis();
    if (!disp_ready || failFull || strcmp(lastState, ps.state) != 0 || strcmp(lastJob, ps.job) != 0 ||
        fiveMinFull) {
      disp_ready = true;                // 全刷后置位（进入时强制全刷）
      snprintf(lastState, sizeof(lastState), "%s", ps.state);
      snprintf(lastJob, sizeof(lastJob), "%s", ps.job);
      renderFull();
    } else {
      for (int i = F_JOB; i <= F_AMS; i++) {   // 字段局刷（vUpdate 内 key 相同会跳过）
        fieldKey(i, buf, sizeof(buf));
        vUpdate(i, buf);
      }
    }

    // 任务结束（finish/idle）→ 切回主界面（手动切换后 5 分钟冷却内不自动切）
    if (ok && psValid &&
        (strcmp(ps.state, "finish") == 0 || strcmp(ps.state, "idle") == 0)) {
      appSwitchAuto(0);
    }
  }
}

// 是否有活跃打印任务（供主界面自动切换：running/pause/prepare 都算活跃）
bool printerIsActive() {
  if (!psValid) return false;
  return strcmp(ps.state, "running") == 0 || strcmp(ps.state, "pause") == 0 ||
         strcmp(ps.state, "prepare") == 0;
}

// 仅拉取 HA 状态更新 ps（不画屏、不触发切换）；主界面定期调用以检测打印任务
void printerPoll() {
  if (WiFi.status() != WL_CONNECTED) return;
  fetchPrinter();          // 更新 ps/psValid（含 AMS 型号首次拉取）
  static bool amsModelFetched = false;
  if (!amsModelFetched && psValid) {
    fetchAmsModel();
    amsModelFetched = true;
  }
}
