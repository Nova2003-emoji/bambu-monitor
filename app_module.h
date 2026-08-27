// app_module.h — 功能模块接口（多模块框架）
// 每个功能模块 = 一个 AppModule 条目（enter/loop），主文件按 runMode 查表分发。
// 新增模块步骤：
//   1) 新建 modules/<name>.h/.cpp（实现 enter()/loop()，内部状态全部 static 隔离）
//   2) 在 app_modules[] 注册表加一行（runMode 唯一）
// 模块内部只依赖：display（sparse_display.h extern）、espWdtFeed（esp32_compat.h）、
// Serial；WiFi/时间等由模块自管（enter 时初始化）。
#pragma once
#include <Arduino.h>

struct AppModule {
  uint8_t runMode;             // eepUserSet.runMode 匹配值（1=打印机监控...）
  const char* name;            // 模块名（日志用）
  void (*enter)();             // 进入模块（首次/切换时调用；可空）
  void (*loop)();              // 每轮 loop 调用（可空）
};

// 模块注册表（主文件引用，数组实现见 app_modules.inc 或各模块头）
extern const AppModule app_modules[];
extern const uint8_t APP_MODULE_COUNT;

// 当前运行模块（主文件持有，loop 按此查表分发；模块内可切换）
extern uint8_t g_runMode;
// 模块内请求切换（如主界面检测到打印任务 → appSwitchTo(1)）
void appSwitchTo(uint8_t runMode);

// 手动切换（按键）：记录冷却时间戳；冷却期内自动切换被抑制
void appSwitchManual(uint8_t runMode);
// 自动切换（任务驱动）：手动切换后 AUTO_SWITCH_COOLDOWN_MS 内被抑制
void appSwitchAuto(uint8_t runMode);
#define AUTO_SWITCH_COOLDOWN_MS  300000   // 手动切换后 5 分钟不自动切换

// 按 runMode 查模块（找不到返回 nullptr）
const AppModule* appModuleByRunMode(uint8_t runMode);
