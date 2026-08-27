// main_module.h — 主界面模块（runMode=0，开机默认）
// 内容：时钟 + 日历（日期/星期）+ 天气（Open-Meteo，Breezy Weather 同源）
// 布局参照 Breezy Weather 主屏：顶栏(城市+天气图标+时钟) → 大温度 header →
// 日期 → 3 天预报 → 底部信息带。QFont16/24 + MDI 天气图标（WeatherIcons_22）。
// 自动切换：检测到打印机活跃任务(running/pause/prepare) → appSwitchTo(1)。
#pragma once

void main_enter();   // 进入模块：WiFi/NTP + 天气首拉 + 全刷（幂等）
void main_loop();    // 每轮 loop：时钟 1s + 天气 15s 节拍 + 字段局刷 + 任务检测
