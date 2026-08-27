// web_config.h — Web 配置服务（仅进配置/配网界面时启动）
// 功能：WebServer 提供配置页（城市名+WiFi+经纬度），NVS 存储；STA 模式用设备 IP 访问，
//       配网（AP）模式下同一服务 + captive portal 重定向（DNS 劫持由 config_module 负责）。
#pragma once

// 启动 Web 配置服务（路由注册 + server.begin；重复调用幂等）
// staMode: true = STA 模式（用当前 IP），false = AP 模式（配 captive portal DNS）
void webCfgBegin(bool staMode);

// 停止 Web 配置服务（离开配置模块时调用；下次 webCfgBegin 重新启动）
void webCfgStop();

// 每轮处理 HTTP 请求（config 模块 loop 里调用）
void webCfgHandle();
