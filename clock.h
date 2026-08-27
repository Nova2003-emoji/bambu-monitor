// clock.h — 共享时钟模块（NTP 校准 + 本地走时）
// 原 main_module/printer_module 各有一套 ntp_base_sec+millis 推算逻辑，合并到此处。
// timeClient 全局实例在 bambu-monitor.ino 定义；NTP 校准节奏 15s 节拍调用 clockSync()。
#pragma once
#include <Arduino.h>

void clockInit();        // timeClient.begin()（模块 enter 调用，幂等）
void clockSync();        // 15s 节拍调用：未同步 forceUpdate 立即校准，同步后每 4 次 update
uint32_t clockNowSec();  // 当前 epoch 秒（本地推算，无网络也能走时）
bool clockSynced();      // NTP 是否已校准
// 1s 节拍走时；分钟变化返回 true 并更新 h/m（供各模块"分钟变即局刷"共用）
bool clockTick(int& h, int& m);
