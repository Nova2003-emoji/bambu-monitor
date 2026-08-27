// chinese_font.h — 中文字库共享封装（gb2312.c 253KB + U8g2 渲染实例）
// 定义在 chinese_font.cpp（唯一编译单元），各模块 extern 引用，避免重复定义。
#pragma once
#include <U8g2_for_Adafruit_GFX.h>

extern const uint8_t chinese_gb2312[253023] U8G2_FONT_SECTION("chinese_gb2312");
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;   // 各模块 main_enter/printer_enter 里 begin(display)

// 检测字符串是否含 CJK（多字节 UTF-8）
static inline bool hasCjk(const char* s) {
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if (*p >= 0x80) return true;
  return false;
}
