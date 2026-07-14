# TokenNest

> DeskNest 本地控制服务。它聚合 ChatGPT/MiniMax 用量，并为 K10 提供
> what2eat 草稿、显式发布和版本化同步接口。

**子项目**：`C:\HinarCode\DeskNest\TokenNest`
**栈**：Node.js 18+（内置 test runner + fetch） + Express
**端口**：`8787`（避开 gesture dashboard 的 `8765`）
**协议**：HTTP / JSON

---

## 为什么单独做一个子项目

- OAuth refresh、5xx 重试、双源去重在 ESP32 上做太重；分层为本机聚合、K10 消费稳定 JSON。
- what2eat 在本机保存草稿和不可变发布快照；K10 只应用比本地 revision 新的发布内容。
- 用户希望"本机小服务"而非"远端 relay"。

---

## 快速开始

```powershell
cd C:\HinarCode\DeskNest\TokenNest
npm install
copy config\minimax.example.json config\minimax.json   # 填入你的 MiniMax apiKey
copy config\tokennest.example.yaml config\tokennest.yaml
copy .env.example .env
npm test
npm start
```

启动后访问：

- `http://127.0.0.1:8787/status.json` — K10 兼容格式，含窗口百分比和明确的可用性字段
- `http://127.0.0.1:8787/api/usage` — 完整 5h/weekly 扩展视图
- `http://127.0.0.1:8787/healthz` — 上游健康度
- `http://127.0.0.1:8787/desk/` — what2eat 本地管理页

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

### what2eat 草稿与发布

what2eat 不包含内置菜品。首次启动时草稿为空，保存草稿不会改变开发板内容；
空草稿可以保存，但不能发布，避免生成开发板无法应用的 revision。
只有显式发布才会创建新的 `revision`。

- `GET /api/what2eat/draft` — 读取草稿、已发布 revision 和最近 ACK；
- `PUT /api/what2eat/draft` — 保存草稿，请求包含当前 `draftVersion`；版本冲突返回 `409`；
- `POST /api/what2eat/publish` — 发布指定 `draftVersion`，返回不可变 envelope；
- `GET /api/what2eat/sync?after=N` — 没有更新返回 `204`，有更新返回 `200` 和完整 envelope；
- `POST /api/what2eat/ack` — 开发板回报 `{revision,status:'applied'|'rejected',error?}`。

`GET/PUT /api/desknest` 继续负责非 what2eat 的本机设置，并在 GET 中附带 AI usage
状态供控制台显示。该接口只包含 `settings` 与 `usage`，不再包含旧的
`menu/today/yesterday/active` 字段；what2eat 始终使用上面的独立发布协议。

发布 envelope：

```json
{
  "schemaVersion": 1,
  "revision": 1,
  "contentHash": "sha256-lowercase-hex",
  "what2eat": {
    "items": [
      { "id": "rice-1", "name": "番茄牛腩饭", "count": 3, "price": "28.50", "score": 85 }
    ]
  }
}
```

`contentHash` 是紧凑 UTF-8 `JSON.stringify(what2eat)` 的 SHA-256 小写十六进制值。
菜名字符集与固件的 `lv_font_16_dynamic` 保持一致：ASCII、常用 CJK、
中文/全角标点及日文假名可发布；emoji 和范围外字符在保存草稿时被拒绝，
避免服务接受但开发板无法显示的内容。
最多 15 项；`id`/`name`/`price` 分别不超过 23/31/9 UTF-8 bytes，
`count` 为 `0..65535`，`score` 为 `10..100` 的 0.5 步进整数（例如 `85` 表示 8.5，
`65` 表示 6.5）。管理页显示并编辑为 1–10 的小数。Count A 语义下，`count` 由模板管理，
板端 B pick 不回写次数。为匹配固件的固定内存 parser，发布内容拒绝控制字符，
菜名还明确拒绝双引号和反斜杠，保证 wire 中不会出现需要固件反转义的文本。
管理页最多提供 15 个编辑槽，空菜名不会进入发布 payload；开发板按 envelope
中的实际 `items.length` 显示非空行，超过可视区域的内容在板端列表视口内滚动，
B pick 只在已校验的非空条目中选择。

写入边界：若设置 `TN_WHAT2EAT_ADMIN_TOKEN`，管理 API 要求
`X-TokenNest-Admin-Token`；未设置时仅允许 loopback 管理请求。
what2eat sync/ack 不再使用设备鉴权 token；它们只适用于当前本地/LAN
控制服务。管理 API 仍受 loopback 或 `TN_WHAT2EAT_ADMIN_TOKEN` 保护。
服务启动时会在 Node 18+ 自动加载项目根目录的 `.env`；系统/NSSM 已注入的环境变量
优先于 `.env`。服务不再向所有来源开放 CORS；
管理页使用同源请求。

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
│       ├── health.js      # /healthz
│       └── what2eat.js    # draft/publish/sync/ack 与原子持久化
├── test/                  # node:test，零外部依赖
├── scripts/               # start.ps1 / install-service.ps1 / probe-*.js
├── config/                # 服务真值忽略；what2eat.example.json 为 tracked 空模板
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
- **K10 实机仍需独立验收**：服务测试只能证明协议与持久化行为，不能替代开发板的 NVS、断网恢复、B pick 和视觉验收。
- **本地 HTTP 无 TLS**：what2eat sync/ack 无设备令牌，管理 API 可选管理员令牌；
  这些边界不等同于公网安全方案，不要把 TokenNest 直接暴露到互联网。

---

## 致谢

- [steipete/CodexBar](https://github.com/steipete/CodexBar) — ChatGPT `wham/usage` 端点 + 字段文档
- [Eyozy/minimax-usage](https://github.com/Eyozy/minimax-usage) — MiniMax 端点参考
- [farion1231/cc-switch](https://github.com/farion1231/cc-switch) — cc-switch 兼容的 status.json 格式
- [NSSM](https://nssm.cc) — Windows 服务包装
