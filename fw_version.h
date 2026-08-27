// fw_version.h — 固件版本信息
// GitHub Actions 打 tag 编译时自动覆盖本文件（真实 tag + UTC 编译时间）；
// 本地直接编译为 "dev" 默认值。CONFIG INFO 页与网页显示用。
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#ifndef FW_BUILT
#define FW_BUILT ""
#endif