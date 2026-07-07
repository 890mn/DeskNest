# 栖屏 DeskNest

> **栖于桌面，息于常亮之间。**

基于行空板 K10（DFRobot UNIHIKER，ESP32-S3）的 AI 用量与桌面生活状态助手。

---

## 项目定位

栖屏 = 一块常驻桌面的 2.8" 小屏。竖起来，它告诉你 AI 月度用量还剩多少；横过来，它变成专注计时；翻过去，它进入"栖息模式"——息于常亮之间。

核心功能：

- **AI 用量哨兵**：ChatGPT Plus/Pro（Codex 订阅）与 MiniMax 用量进度一眼可见
- **桌面环境站**：温湿度、光照、舒适度
- **翻面栖息**：物理翻过屏即"息"，再翻回数据仍在
- **手势导航**：摇一摇、翻一翻、按一按
- **cc-switch 集成**：复用 12 万星开源项目，K10 不存任何敏感 API key

---

## 项目信息

| 项 | 值 |
|----|----|
| 中文名 | 栖屏 |
| 英文名 | DeskNest |
| 副标题 | 基于行空板 K10 的 AI 用量与桌面生活状态助手 |
| Slogan | 栖于桌面，息于常亮之间 |
| 工程目录 | `C:\HinarCode\DeskNest` |
| Arduino 入口 | `DeskNest.ino` |
| PlatformIO 环境 | `desknest_k10` |
| WiFi AP | `DeskNest-XXXX` |
| 命名空间 | `desknest` |
| 代码前缀 | `dn_` |

---

## 快速开始

### PlatformIO（推荐）

```bash
cd C:\HinarCode\DeskNest
pio run -e desknest_k10           # 编译
pio run -e desknest_k10 -t upload # 烧录
pio device monitor                # 串口监视
```

### Arduino IDE

1. 安装 K10 BSP（见 [docs/architecture.md]）
2. 打开 `DeskNest.ino`
3. Sketch → Upload

### Mind+

- 直接烧录 `mindplus_assets/prebuilt/desknest_v0.1.0.bin`
- 或用 `mindplus_assets/UhlCore/` 积木项目

---

## 当前状态

- [x] P0-A 工程骨架（串口心跳）
- [ ] P0-B 传感器通路
- [ ] P0-C 4 轴状态机
- [ ] P0-D UI 8 页面渲染
- [ ] P0-E 翻面栖息
- [ ] P0-F 端到端 demo
- [ ] P1-A WiFi 配网
- [ ] P1-B cc-switch 同步
- [ ] P1-C 告警与缓存
- [ ] P1-D 专注页
- [ ] P1-E 文档

---

## 完整文档

- 完整计划：[`C:\Users\DF\.claude\plans\elegant-skipping-waterfall.md`](C:\Users\DF\.claude\plans\elegant-skipping-waterfall.md)
- 用户开发规格：`docs/user-spec.md`（从 `new.md` 移入）
- 架构说明：`docs/architecture.md`（P1-E 阶段补全）
- 引脚表：`docs/pinmap.md`（P0-A 复核后填入）
- 电源预算：`docs/power-budget.md`（P0-E 阶段补全）
- cc-switch 集成：`docs/cc-switch-integration.md`（P1-B 阶段补全）
- 演示脚本：`docs/demo-script.md`（P1-E 阶段补全）

---

## 致谢

- [DFRobot](https://www.dfrobot.com/) — UNIHIKER K10 与 `unihiker_k10.h` 库
- [farion1231/cc-switch](https://github.com/farion1231/cc-switch) — 用量追踪思路与本地代理
- [Espressif](https://www.espressif.com/) — ESP32-S3 与 Arduino 内核
