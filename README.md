# 栖屏 DeskNest

> **栖于桌面，息于常亮之间。**

基于行空板 K10（DFRobot UNIHIKER，ESP32-S3）的 AI 用量与桌面生活状态助手。

---

## 项目定位

栖屏 = 一块常驻桌面的 2.8" 小屏
竖起来，它告诉你 AI 月度用量还剩多少
翻过去，它进入"栖息模式"——息于常亮之间。

核心功能：

- **AI 用量哨兵**：ChatGPT Plus/Pro（Codex 订阅）与 MiniMax 用量进度一眼可见
- **本地用量聚合**：[`TokenNest/`](TokenNest/README.md) Node.js 子项目，本机后台拉用量，K10 未来通过 HTTP 拉
- **桌面环境站**：温湿度、光照、舒适度
- **翻面栖息**：物理翻过屏即"息"，再翻回数据仍在
- **手势导航**：摇一摇、翻一翻、按一按

---

## 快速开始

### PlatformIO（推荐）

```bash
cd C:\HinarCode\DeskNest
pio run -e desknest_k10           # 编译
pio run -e desknest_k10 -t upload # 烧录
pio device monitor                # 串口监视
```

**首次编译前**：WiFi SSID/密码与 TokenNest URL 是个人凭证，**不要**进 git。
脚本 `scripts/pio_local_config.py` 会在第一次 `pio run` 时自动从 `platformio.local.ini.example` 复制一份 `platformio.local.ini` 并打印提示。复制完后**编辑** `platformio.local.ini` 填入真实值即可。该文件已被 `.gitignore` 排除。

### Arduino IDE

1. 安装 K10 BSP（见 [docs/architecture.md]）
2. 打开 `DeskNest.ino`
3. Sketch → Upload

### Mind+

- 直接烧录 `mindplus_assets/prebuilt/desknest_v0.1.0.bin`
- 或用 `mindplus_assets/UhlCore/` 积木项目

## CNFontNest font pre-build

DeskNest runs the sibling `CNFontNest` repository as a PlatformIO `pre:`
script for the `DeskNest` firmware environment. The tracked configuration is
`tools/cnfontnest.json`; generated manifests stay in the ignored
`.cnfontnest/` directory, while the owned LVGL C font outputs remain under
`src/fonts/`.

The default sibling path is `..\CNFontNest`. To use another checkout, set
`CNFONTNEST_ROOT` before building. The adapter verifies the configured
`lv_font_conv` version and fails the firmware build when the tool or generated
artifact is unavailable. The host `desknest_test` environment does not run
this font pre-build step.

```powershell
$env:CNFONTNEST_ROOT = "C:\path\to\CNFontNest"
C:\Users\DF\.platformio\penv\Scripts\pio.exe run -e DeskNest
```
