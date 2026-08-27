// text_render.h — 通用文本渲染（自动 CJK 检测，QFont16/24 统一入口）
// uiDrawText：含中文 → U8g2 gb2312（y+18 基线，透明模式）；纯 ASCII → QFont
// uiTextWidth：同步测宽（居中/贴右用），含 CJK → U8g2 getUTF8Width，纯 ASCII → qfontWidth
// 依赖 chinese_font（字库实例唯一编译单元）+ qfont16（QFONT/QFont 定义）
#pragma once
#include "chinese_font.h"
#include "qfont16.h"

// 自动 CJK 检测渲染（s 含任意多字节字符走 U8g2，否则 QFont）
void uiDrawText(int16_t x, int16_t y, const QFONT* f, const char* s, uint16_t ink);

// 强制中文字库渲染（gb2312，16px；所有模块统一用此字体）
void uiDrawTextU8g2(int16_t x, int16_t y, const char* s, uint16_t ink);

// 同步测宽（与 uiDrawText 的绘制宽度一致）
int16_t uiTextWidth(const QFONT* f, const char* s);

// 强制中文字库测宽（与 uiDrawTextU8g2 一致）
int16_t uiTextWidthU8g2(const char* s);
