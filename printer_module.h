// printer_module.h — 3D 打印机监控模块（runMode=1）
// 独立编译单元（.cpp），内部状态全部 static 隔离；对外只暴露模块入口。
// 依赖主文件全局：display(sparse_display.h)、espWdtFeed(esp32_compat.h)、
// WiFi/timeClient/sta_ssid（extern，见 printer_module.cpp 顶部）。
#pragma once

void printer_enter();   // 进入模块：WiFi 连接 + NTP 初始化（幂等，可重复调用）
void printer_loop();    // 每轮 loop：时钟 1s 走时 + HA 拉取 15s 节拍 + 字段局刷
bool printerIsActive(); // 是否有活跃打印任务（供主界面自动切换）
void printerPoll();     // 仅拉取 HA 状态更新 ps（不画屏；主界面定期调用以检测任务）
