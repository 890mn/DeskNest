# DeskNest V1.0 用户手册

> English quick reference is available in the final section.

本文面向已经拿到 UNIHIKER K10、准备运行 DeskNest V1.0 的使用者。构建和配置入口以仓库根目录的 `README.md` 为准；这里重点说明日常使用、页面、设置和故障判断。

## 1. 启动前检查

1. 电脑与 K10 连接到同一可信局域网。
2. TokenNest 已运行，电脑本机能够打开 `http://127.0.0.1:8787/healthz`。
3. `platformio.local.ini` 中填写的是电脑局域网 IPv4，而不是 `127.0.0.1`。
4. 固件已经通过 `DeskNest` 环境编译并烧录。
5. 串口波特率为 `115200`；启动日志应最终进入 `ACTIVE`。

TokenNest 不可用时，K10 应保留缓存或显示不可用状态；空值和 `0%` 不应被理解为服务一定正常。

## 2. 页面顺序

V1.0 的竖屏页面由 `PageRegistry` 管理，当前顺序如下：

1. **DeskNest 总览**：AI 使用情况、温湿度、光照与桌面生活状态的组合首页。
2. **AI Left**：ChatGPT/Codex 与 MiniMax 的用量窗口、重置时间和状态。
3. **what2eat**：浏览器发布的候选菜品、价格、评分、次数和当前推荐。
4. **Settings**：首页焦点、AI 提醒阈值与省电模式。

源码保留了独立 Environment renderer，但它不在 V1.0 的 `PageRegistry` 导航组中；环境信息以总览卡片为正式入口，不应把保留 renderer 当成第五个可访问页面。

设备翻到正面朝下时进入特殊栖息状态；它不属于普通页面循环。

## 3. 默认操作

V1.0 默认采用手势优先模式。为了降低桌面震动造成的误触，导航手势默认需要同时按住 A 确认。

| 场景 | 操作 | 结果 |
| --- | --- | --- |
| 任意普通页面 | 按住 A，向左摇动 | 上一页 |
| 任意普通页面 | 按住 A，向右摇动 | 下一页 |
| 任意普通页面 | 短按 A | 开关手势确认门；关闭后摇动不再要求按住 A |
| 任意普通页面 | 长按 B 约 1 秒 | 返回 DeskNest 总览 |
| what2eat | 短按 B | `B pick`，在有效非空项目中重新选择 |
| Settings | 短按 A | 选择下一行 |
| Settings | 短按 B | 切换当前行并立即写入 NVS |
| 任意页面 | 翻到正面朝下 | 进入栖息状态并屏蔽普通输入 |
| 栖息状态 | 翻回正面 | 唤醒并恢复翻面前页面 |

按键或被接受的手势也会唤醒因空闲而熄灭的屏幕。

## 4. 设置说明

### 首页焦点

- `自动`：每 30 秒在 AI、生活和极简信息之间轮换。
- `AI优先`：优先显示 Codex/ChatGPT 重置日期。
- `生活优先`：优先显示当前 what2eat 推荐。
- `极简`：仅保留本地时间。

### AI 提醒

可选 `70% / 80% / 85% / 90%`。有效 AI 用量达到或超过阈值时进入提醒状态。默认值为 `80%`。

### 省电模式

| 模式 | 进入环境态 | 熄灭背光 |
| --- | ---: | ---: |
| 标准 | 30 秒 | 90 秒 |
| 省电 | 15 秒 | 45 秒 |
| 常亮 | 不因空闲熄屏 | 不因空闲熄屏 |

K10 当前只使用二值背光开关；环境态不是 PWM 调光承诺。翻面状态和普通空闲熄屏是两条不同路径。

## 5. 管理 what2eat

1. 在运行 TokenNest 的电脑上打开 `http://127.0.0.1:8787/desk/`。
2. 编辑候选项并保存草稿。空白行不会进入发布内容。
3. 点击发布。只有发布才会产生新的 revision；仅保存草稿不会改变开发板。
4. K10 拉取比本地 revision 更新的内容，校验后写入双槽 NVS 缓存并提交 ACK。
5. 板端 `B pick` 只改变当前选择，不修改模板里的次数。

V1.0 最多接受 15 个非空项目。评分为 1–10，步进 0.5。动态文本不接受 emoji；超出固件字符集或长度限制的内容会被服务拒绝。

管理写入默认只允许电脑本机访问；配置 `TN_WHAT2EAT_ADMIN_TOKEN` 后，可在受信网络中使用管理令牌。同步与 ACK 没有设备令牌，因此 TokenNest 不得直接暴露到公网。

## 6. AI 用量的含义

- 5 小时窗口存在时，页面优先使用 5 小时百分比。
- 没有 5 小时窗口但存在 weekly 窗口时，页面显示 weekly 的真实百分比，并可将 5 小时位置标为 `NO LIMIT`。
- `0%`、缺少重置时间、对象缺失和服务失败是不同状态，不能互相替代。
- TokenNest 的 `/status.json` 是 K10 的紧凑协议；`/api/usage` 是诊断用扩展视图，V1.0 固件不直接消费它。

## 7. 常见问题

### K10 一直没有在线数据

- 确认 URL 使用电脑局域网 IP；
- 检查 Windows 防火墙是否允许 TCP `8787`；
- 在另一台同 WiFi 设备上访问 `http://<电脑IP>:8787/status.json`；
- 重启 TokenNest，确认服务账户能够读取所需配置；
- 修改 `platformio.local.ini` 后重新编译并烧录，避免继续使用旧固件。

### 固件构建提示找不到 CNFontNest

确保 CNFontNest 位于 DeskNest 的同级目录，或先设置：

```powershell
$env:CNFONTNEST_ROOT = 'C:\path\to\CNFontNest'
```

同时确认 `lv_font_conv --version` 与 `tools/cnfontnest.json` 声明的 `1.5.3` 一致。

### 菜名出现方框或无法发布

服务会拒绝明显超出 V1.0 字符边界的文本，但字体覆盖仍是有限集合。先缩短名称并去掉 emoji、罕见扩展字符、双引号和反斜杠。不要把构建成功当作任意动态文本都能显示的证明。

### 摇动有动画但不切页

默认确认门开启时需要按住 A。若已经按住，仍应检查回程和回稳动作，而不是只降低灵敏度。短按 A 可以切换确认门，串口日志会显示当前状态。

### 设置修改后重启丢失

这属于 V1.0 的实机验收失败，而不是正常限制。记录设置行、修改值、重启方式和串口日志，再检查 NVS 写入结果。

## English quick reference

DeskNest V1.0 has four navigable portrait pages: Overview, AI Left, what2eat, and Settings. Environment data is integrated into Overview; the reserved Environment renderer is not part of the V1.0 page registry. Hold A while shaking left/right to navigate; short A toggles the gesture-confirmation gate; hold B to return home. On what2eat, short B picks another item. On Settings, A selects a row and B changes and persists its value. Turning the K10 face down enters rest mode; turning it back restores the previous page. TokenNest must run on a trusted LAN, and the board URL must use the PC's LAN IP.
