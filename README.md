# bambu-monitor — ESP32 墨水屏 Bambu Lab 打印机监控

基于 **ESP32 + 4.2 英寸黑白墨水屏**的 3D 打印机实时监控与桌面信息面板。
数据来自 Home Assistant 的 Bambu Lab 官方集成，本地网络运行、无需云服务。

## 功能

三个界面，用 IO0 按键循环切换；打印开始自动切到监控页，打印结束回到主界面。

**主界面**
- 时钟、日历（含星期）
- 7 天天气预报（Open-Meteo，免费免 key）：天气图标、降水概率、中文说明、高低温

**打印监控**
- 任务名（过长自动折两行）、进度条、百分比、中文状态词
- 层数、喷嘴 / 热床的当前与目标温度
- 剩余时间、预计结束时间
- 4 台 AMS 槽位：耗材型号与余量、AMS 温湿度

**配置 / 配网**
- 左右分栏：左侧 WiFi/IP/城市，右侧日期、时间、固件版本与编译时间
  （`FW v0.0.17` = CI 构建；`FW LOCAL` = 本地构建）
- 断网时长按 IO0 进入 AP 网页配网（captive portal，填写 WiFi 与城市）

## 网页刷写 & 配网

项目提供网页版工具（Chrome / Edge 打开）：**https://nova2003-emoji.github.io/bambu-monitor/**

1. **连接串口** — 浏览器弹出列表选择设备
2. **选择固件** — 在线最新版 / 历史版本 / 本地文件
3. **刷写** — 两种模式：
   - 固件更新（推荐）：只写程序区，约 1-2 分钟，**保留设备配置**
   - 全量刷写：清除全部配置，恢复出厂
4. **配网** — 填 WiFi / HA 令牌 / 城市与实体前缀（留空自动识别），保存后重启生效

页面通过 Web Serial 直连设备，数据只在本机与设备之间，不上传任何服务器。

## 快速开始（本地开发）

1. 复制 `config.example.h` 为 `config.h`，填入 WiFi 名称/密码与 Home Assistant 长期令牌（`config.h` 不入库）
2. 编译烧录（见下）
3. 首次启动后实体前缀自动发现（也可在配置页手动填写）

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
arduino-cli upload -p <COM> --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" .
```

依赖库：GxEPD2（需 Z96 补丁，见 `patches/`）、U8g2_for_Adafruit_GFX、Adafruit GFX、Adafruit BusIO、ArduinoJson、NTPClient、Time。

## 发布流程（维护者）

```bash
# 1. 同步源码到发布仓（当前分支的 projects/bambu-monitor → 发布仓根目录）
bash sync-github.sh          # 仅更新代码与网页，不触发编译

# 2. 发布新版本（打 tag 触发 CI 编译 + Release）
git -C .zcode/tmp/bambu_repo tag vX.Y.Z
git -C .zcode/tmp/bambu_repo push origin vX.Y.Z

# 3. 刷新网页托管的固件（Pages 部署自动拉取最新 Release）
git -C .zcode/tmp/bambu_repo commit --allow-empty -m "chore: redeploy pages"
git -C .zcode/tmp/bambu_repo push origin main
```

- **CI（build workflow）**：仅 `v*` tag 触发——编译固件，生成 `version.json`（版本/UTC 编译时间/commit）与 `fw_version.h`（固件内嵌版本信息），一并发布到 Release
- **Pages（pages workflow）**：push main 触发——部署 `web/` 目录到 GitHub Pages，并拉取最新固件与最近 Release 到 `web/firmware/`（同源托管，供网页直接下载）
- 发布仓为独立仓库（源码快照），本地工作区通过 `sync-github.sh` 单向同步

## 参考与借鉴

本项目站在这些开源项目/资料上，感谢原作者：

| 项目 / 资料 | 链接 | 借鉴点 |
|---|---|---|
| ESP32-eInk-Dashboard（原版拓竹墨水屏监控） | https://github.com/VoIPshare/ESP32-eInk-Dashboard | 稀疏字体渲染（SparseGFXfont）、MDI 图标绘制、监控界面布局 |
| weather-ink-screen（甘草 WeatherInk 固件） | https://gitee.com/lichengjiez/weather-ink-screen | ESP8266→ESP32 移植基线、时钟/界面模块结构 |
| Breezy Weather | https://github.com/papjul/breezy-weather | 主界面天气 UI 布局参考（7 天预报横排） |
| Material Design Icons（MDI） | https://pictogrammers.com/library/mdi/ | 天气/温度图标（OFL 许可，自行从 TTF 提取为位图） |
| Open-Meteo API | https://open-meteo.com | 免费免 key 的天气/地理编码数据源 |
| GxEPD2 | https://github.com/ZinggJM/GxEPD2 | SSD1619 墨水屏驱动与全刷/局刷框架 |
| esptool-js | https://github.com/espressif/esptool-js | 网页刷写（Web Serial + write_flash） |
| U8g2（含 gb2312 中文字库） | https://github.com/olikraus/u8g2 | 中文/混合文字渲染（基线定位、宽度测量） |
| Howard Hinnant date algorithms | https://howardhinnant.github.io/date_algorithms.html | civil_from_days / days_from_civil 日期换算 |
