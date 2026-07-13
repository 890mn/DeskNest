# TokenNest

> 本地端 AI 用量聚合服务。为 DeskNest K10 固件提供 ChatGPT Plus/Pro（5h + weekly） 和 MiniMax Token Plan（5h + weekly） 的统一查询接口。

**子项目**：`C:\HinarCode\DeskNest\TokenNest`
**栈**：Node.js 18+（内置 test runner + fetch） + Express
**端口**：`8787`（避开 gesture dashboard 的 `8765`）
**协议**：HTTP / JSON

---

## 为什么单独做一个子项目

- K10 端目前**没有 HTTP 客户端**（plan P1-A / P1-B 还没实现），所有用量数据走 LittleFS 上的 `/cc-switch/status.json` 缓存。
- OAuth refresh、5xx 重试、双源去重在 ESP32 上做太重；分层 — 本机聚合、K10 只渲染。
- 用户希望"本机小服务"而非"远端 relay"。

---

## 快速开始

```powershell
cd C:\HinarCode\DeskNest\TokenNest
npm install
copy config\minimax.example.json config\minimax.json   # 填入你的 MiniMax apiKey
copy config\tokennest.example.yaml config\tokennest.yaml
npm test
npm start
```

启动后访问：

- `http://127.0.0.1:8787/status.json` — K10 兼容格式，含窗口百分比和明确的可用性字段
- `http://127.0.0.1:8787/api/usage` — 完整 5h/weekly 扩展视图
- `http://127.0.0.1:8787/healthz` — 上游健康度

---

## 凭证准备

### ChatGPT

TokenNest **不读浏览器 cookie**（LevelDB 加密、DPAPI 受保护），而是复用 `codex` CLI 的 OAuth 结果：

```powershell
npx -y @openai/codex auth login
```

登录后，token 落到 `%USERPROFILE%\.codex\auth.json`，TokenNest 自动读。

### MiniMax

到 <https://platform.minimaxi.com> 申请 API key，填到 `config\minimax.json`：

```json
{ "apiKey": "eyJhbGciOiJSUzI1NiIsIn..." }
```

> ⚠️ 该文件已被 .gitignore 排除，**不要**提交到 git。

---

## 开机自启

详见 [docs/STARTUP.md](docs/STARTUP.md)。简要：

```powershell
# 1. 下载 NSSM (https://nssm.cc)，把 nssm.exe 加到 PATH
# 2. 装为 Windows 服务
powershell -ExecutionPolicy Bypass -File scripts\install-service.ps1
# 3. 重启电脑，30s 内服务自启
```

---

## 接口契约

详见 [docs/WIRE_FORMAT.md](docs/WIRE_FORMAT.md)。

**`GET /status.json`** — K10 端 `dn_ai_usage_parse_cc_switch_status` 能直接解析：

```json
{
  "updatedAtText": "12 min",
  "warningText": "",
  "chatgpt":  { "percent": 42, "weeklyPercent": 11, "fiveHourAvailable": true, "weeklyAvailable": true },
  "minimax":  { "percent": 30, "weeklyPercent": 18, "fiveHourAvailable": true, "weeklyAvailable": true }
}
```

**`GET /api/usage`** — 5h/weekly 双窗口完整 dump，给 K10 未来 P1-D 消费：

```json
{
  "fetchedAt": "2026-07-08T12:34:56Z",
  "primaryPercent": 42,
  "secondaryPercent": 18,
  "chatgpt": { "ok": true, "primary": {"usedPercent":42,"resetsInSeconds":1234}, "secondary": {...}, "extras": [...] },
  "minimax": { "ok": true, "primary": {...}, "secondary": {...}, "models": [...] }
}
```

---

## 目录

```
TokenNest/
├── src/
│   ├── server.js          # 入口
│   ├── config.js          # 读 yaml + env
│   ├── http.js            # 带重试的 fetch 封装
│   ├── logger.js          # [D][TOK] 日志
│   ├── sources/
│   │   ├── chatgpt.js     # wham/usage + OAuth refresh
│   │   ├── minimax.js     # coding_plan/remains
│   │   └── aggregator.js  # 双源归一化
│   ├── cache/store.js     # 文件 JSON 缓存
│   └── routes/
│       ├── status.js      # /status.json（K10 兼容）
│       ├── usage.js       # /api/usage（5h/weekly 扩展）
│       └── health.js      # /healthz
├── test/                  # node:test，零外部依赖
├── scripts/               # start.ps1 / install-service.ps1 / probe-*.js
├── config/                # tokennest.yaml + minimax.json（git 忽略真值）
├── docs/                  # WIRE_FORMAT.md / STARTUP.md
└── package.json
```

---

## 命名约定

- 内部变量 `tn_*`，和主项目 `dn_*` 区分
- 日志统一 `[D][TOK] [tag]` 前缀
- HTTP 路径 `/api/...` 前缀

---

## 已知限制

- **MiniMax 接口真实性未亲自验证**：全网唯一参考实现是 `Eyozy/minimax-usage`，字段命名反直觉（`usage_count` 实际是"剩余"）。第一次跑一定要 `npm run probe:minimax` 抓真响应，对照字段名。
- **K10 端 5h/weekly 显示已接入**：`/status.json` 的窗口可用性和有效百分比已经进入 DeskNest parser/UI；未重新烧录时，实机显示仍需单独验收。
- **K10 端 HTTP 客户端未实现**：本服务先按 HTTP 设计，等 P1-B 把 `nvs_key::CC_SWITCH_URL` 消费起来后即插即用。

---

## 致谢

- [steipete/CodexBar](https://github.com/steipete/CodexBar) — ChatGPT `wham/usage` 端点 + 字段文档
- [Eyozy/minimax-usage](https://github.com/Eyozy/minimax-usage) — MiniMax 端点参考
- [farion1231/cc-switch](https://github.com/farion1231/cc-switch) — cc-switch 兼容的 status.json 格式
- [NSSM](https://nssm.cc) — Windows 服务包装
