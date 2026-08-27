// app_modules.cpp — 功能模块注册表
// 新增模块：include 其头文件，在 app_modules[] 加一行（runMode 唯一）。
#include "app_module.h"
#include "main_module.h"
#include "printer_module.h"
#include "config_module.h"

const AppModule app_modules[] = {
  { 0, "main",    main_enter,    main_loop },
  { 1, "printer", printer_enter, printer_loop },
  { 2, "config",  config_enter,  config_loop },
};
const uint8_t APP_MODULE_COUNT = sizeof(app_modules) / sizeof(app_modules[0]);

const AppModule* appModuleByRunMode(uint8_t runMode) {
  for (uint8_t i = 0; i < APP_MODULE_COUNT; i++)
    if (app_modules[i].runMode == runMode) return &app_modules[i];
  return nullptr;
}
