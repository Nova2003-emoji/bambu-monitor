// chinese_font.cpp — 中文字库唯一编译单元（gb2312.c 253KB + U8g2 渲染实例）
// 各模块 include chinese_font.h 引用 extern，避免 gb2312.c 跨编译单元重复定义。
#include "chinese_font.h"
#include "fonts/gb2312.c"
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
