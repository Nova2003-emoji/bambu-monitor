// text_render.cpp — 通用文本渲染实现（唯一编译单元）
// uiDrawText/uiTextWidth 实体；依赖 chinese_font（u8g2Fonts）与 qfont16（QFONT 渲染）。
#include "sparse_display.h"   // extern display（drawQFontString 需要）
#include "text_render.h"

// 自动 CJK 检测渲染：含多字节字符 → U8g2 gb2312（基线 y+18）；纯 ASCII → QFont
void uiDrawText(int16_t x, int16_t y, const QFONT* f, const char* s, uint16_t ink) {
  if (hasCjk(s)) {
    u8g2Fonts.setFontMode(1);          // 透明模式（背景不画）
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(ink);
    u8g2Fonts.setFont(chinese_gb2312);
    u8g2Fonts.setCursor(x, y + 18);    // U8g2 基线在底部
    u8g2Fonts.print(s);
  } else {
    drawQFontString(x, y, f, s, ink);
  }
}

// 同步测宽：含 CJK → U8g2 getUTF8Width（需先 setFont）；纯 ASCII → qfontWidth（含 °C 连体规则）
int16_t uiTextWidth(const QFONT* f, const char* s) {
  if (hasCjk(s)) {
    u8g2Fonts.setFont(chinese_gb2312);
    return u8g2Fonts.getUTF8Width(s);
  }
  return qfontWidth(f, s);
}

// 强制中文字库渲染（gb2312 16px；所有模块统一用此字体）
void uiDrawTextU8g2(int16_t x, int16_t y, const char* s, uint16_t ink) {
  u8g2Fonts.setFontMode(1);          // 透明模式
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setForegroundColor(ink);
  u8g2Fonts.setFont(chinese_gb2312);
  u8g2Fonts.setCursor(x, y + 18);    // U8g2 基线在底部
  u8g2Fonts.print(s);
}

// 强制中文字库测宽（与 uiDrawTextU8g2 一致）
int16_t uiTextWidthU8g2(const char* s) {
  u8g2Fonts.setFont(chinese_gb2312);
  return u8g2Fonts.getUTF8Width(s);
}
