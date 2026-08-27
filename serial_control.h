// serial_control.h — 串口控制
// 命令: 0=主界面 1=打印机 2=配置 r=重启(全刷) h=帮助；serialCmdHook 可挂额外命令
#pragma once
#include <Arduino.h>

static void (*serialCmdHook)(char) = nullptr;   // 额外命令回调（应用层设置）
extern void appSwitchManual(uint8_t runMode);   // 主文件定义（手动切换）

static void serialControl() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() == 0) continue;
      line.trim();
      char cmd = line[0];
      if (cmd >= '0' && cmd <= '2') {
        uint8_t m = cmd - '0';
        Serial.printf("[SERIAL] switch to mode %d\n", m);
        appSwitchManual(m);
      } else if (cmd == 'r' || cmd == 'R') {
        // 无跨模块全局刷新标志：重启即全刷（墨水屏上电必然全刷）
        Serial.println("[SERIAL] restart for full refresh...");
        delay(300);
        ESP.restart();
      } else if (cmd == 'h' || cmd == 'H') {
        Serial.println("[SERIAL] cmds: 0=main 1=printer 2=config r=restart h=help");
      } else if (serialCmdHook) {
        serialCmdHook(cmd);          // 应用层扩展命令
      } else {
        Serial.printf("[SERIAL] unknown cmd: %c (h for help)\n", cmd);
      }
      line = "";
    } else if (line.length() < 32) {   // 防串口垃圾无限累积
      line += c;
    }
  }
}
