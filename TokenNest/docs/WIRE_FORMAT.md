# TokenNest ↔ K10 协议

> Wire format for DeskNest V1.0. K10 解析器以 `src/ai_usage_module.h` 中的 `dn_ai_usage_parse_cc_switch_status()` 为事实权威；TokenNest 输出以 `TokenNest/src/routes/status.js` 为事实权威。

## 1. `GET /status.json`

这是 V1.0 K10 直接消费的紧凑 JSON。TokenNest 当前输出：

```json
{
  "updatedAtText": "2026-07-15 14:32",
  "warningText": "",
  "serverNow": "2026-07-15T06:32:00+08:00",
  "chatgpt": {
    "percent": 42,
    "weeklyPercent": 11,
    "fiveHourAvailable": true,
    "weeklyAvailable": true,
    "fiveHourExpireAt": "2026-07-15T08:30:00+08:00",
    "weekExpireAt": "2026-07-20T00:00:00+08:00"
  },
  "minimax": {
    "percent": 30,
    "weeklyPercent": 18,
    "fiveHourAvailable": true,
    "weeklyAvailable": true,
    "fiveHourExpireAt": "2026-07-15T09:00:00+08:00",
    "weekExpireAt": "2026-07-20T00:00:00+08:00"
  },
  "codexResets": [
    { "name": "Codex RE1", "expireAt": "2026-07-27T00:00:00+08:00" }
  ]
}
```

示例只表达字段形状；时间值不是固定测试数据，也不应复制进业务配置。

### 顶层字段

| 字段 | 类型 | 当前输出 | K10 行为 |
| --- | --- | --- | --- |
| `updatedAtText` | string | 是 | 展示更新时间；缺失时兼容 `updatedAt`，再缺失使用 `cached` |
| `warningText` | string | 是 | 聚合上游错误或 stale 摘要；缺失时兼容 `warning` |
| `serverNow` | string | 是 | 作为板端时间基准输入；空字符串表示不可用 |
| `chatgpt` | object | 是 | ChatGPT/Codex 订阅窗口 |
| `minimax` | object | 是 | MiniMax 窗口 |
| `codexResets` | array | 是 | 最多解析 4 个可用重置卡 |
| `nextRefreshInSec` | integer | 否 | 解析器兼容该字段，当前 TokenNest route 不发送 |

### 用量对象

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `percent` | 0–100 integer | 5 小时窗口已用百分比 |
| `weeklyPercent` | 0–100 integer | weekly 窗口已用百分比 |
| `fiveHourAvailable` | boolean | 是否确认存在 5 小时窗口 |
| `weeklyAvailable` | boolean | 是否确认存在 weekly 窗口 |
| `fiveHourExpireAt` | string or null | 5 小时窗口的绝对重置时间 |
| `weekExpireAt` | string or null | weekly 窗口的绝对重置时间 |

有效百分比的选择规则：5 小时窗口可用时使用 `percent`；否则在 weekly 可用时使用 `weeklyPercent`；两个窗口都未知或失败时按未知状态处理。`0%` 不能单独证明“无限”或“健康”。

TokenNest 不发送 `status` 或 `detail` 展示字符串。K10 根据服务名、窗口可用性、百分比和绝对时间构建展示模型。

### `codexResets`

每项包含：

```json
{ "name": "Codex RE1", "expireAt": "2026-07-27T00:00:00+08:00" }
```

K10 固定内存最多保留 4 项。对象缺少名称或时间时，解析器只在至少一个字段非空时保留该项。

## 2. `GET /api/usage`

这是 TokenNest 的扩展诊断视图，包含聚合时间、两类窗口、上游状态、stale、错误与模型列表。V1.0 固件不直接消费此接口。

典型顶层形状：

```json
{
  "fetchedAt": "2026-07-15T06:32:00Z",
  "primaryPercent": 42,
  "secondaryPercent": 18,
  "chatgpt": {
    "ok": true,
    "stale": false,
    "ageSec": 12,
    "primary": { "usedPercent": 42, "resetsInSeconds": 1234 },
    "secondary": { "usedPercent": 11, "resetsInSeconds": 604800 },
    "extras": [],
    "error": null
  },
  "minimax": {
    "ok": true,
    "stale": false,
    "ageSec": 8,
    "models": [],
    "error": null
  }
}
```

不要让 K10 与调试接口建立第二套展示契约；需要改变板端字段时，应先修改 `/status.json` contract tests 和固件解析器。

## 3. `GET /healthz`

返回每个上游 source 的 `ok`、`ageSec`、`stale` 和 `error`。任一 source 失败或 stale 时返回 HTTP `503`，全部健康时返回 `200`。

## 4. what2eat 协议

what2eat 不复用 `/status.json`：

- `GET /api/what2eat/draft`：管理端读取草稿、发布 revision 和最近 ACK；
- `PUT /api/what2eat/draft`：按 `draftVersion` 保存草稿；冲突返回 `409`；
- `POST /api/what2eat/publish`：显式发布不可变 envelope；
- `GET /api/what2eat/sync?after=N`：没有更新返回 `204`，有更新返回完整 envelope；
- `POST /api/what2eat/ack`：板端提交 `applied` 或 `rejected` 结果。

发布 envelope：

```json
{
  "schemaVersion": 1,
  "revision": 7,
  "contentHash": "sha256-lowercase-hex",
  "what2eat": {
    "items": [
      {
        "id": "rice-1",
        "name": "番茄牛腩饭",
        "count": 3,
        "price": "28.50",
        "score": 85
      }
    ]
  }
}
```

V1.0 最多 15 项；空名称不进入 payload；评分 `85` 表示 `8.5`。服务端 schema、字符串长度、字符边界与固件固定内存 parser 必须保持一致。

管理草稿和发布受 loopback 或 `TN_WHAT2EAT_ADMIN_TOKEN` 保护。sync/ACK 不使用设备令牌，只允许部署在可信 LAN，不能直接暴露到公网。

## 5. 回归要求

协议修改至少需要：

1. TokenNest route / source 测试通过；
2. 固件解析器 host 测试通过；
3. 空、缺失、stale、失败和 weekly-only 输入各有明确预期；
4. what2eat 版本冲突、空发布、最大 15 项和 ACK 失败路径通过；
5. K10 实机验证缓存、断网恢复、时间显示和动态中文。

## English summary

`/status.json` is the compact V1.0 K10 contract; `/api/usage` is a diagnostic view and is not consumed directly by the firmware. The board chooses the 5-hour percentage when that window is available, otherwise the confirmed weekly percentage. what2eat uses a separate versioned draft/publish/sync/ACK protocol. TokenNest is a trusted-LAN service, not a public-internet API.
