// footer.h — 底部统一样式（NTP 左 / 时钟中 / WIFI 右），所有界面共用
#pragma once

#define FOOT_Y 270   // 底部文字顶（与各界面底部一致）

// 绘制底部栏（需 display 已初始化；时钟走 clockNowSec，NTP/WIFI 状态实时读取）
void drawFooter();
