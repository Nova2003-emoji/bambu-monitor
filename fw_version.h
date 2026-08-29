// fw_version.h — 固件版本信息
// CI 编译（打 v* tag）→ "CI v0.0.13 2026-08-28 12:34:56 UTC commit=a1b2c3d"
// 本地 arduino-cli 编译       → "LOCAL 2026-08-28 20:15:30 commit=dirty/local"
// 显示在 CONFIG INFO 页与网页。
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "LOCAL"
#endif

#ifndef FW_BUILT
#define FW_BUILT __DATE__ " " __TIME__ " local"
#endif
