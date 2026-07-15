# TokenNest 启动指南

> 三种方式：临时启动、推荐的本机常驻服务、纯手动。

---

## A. 临时启动（开发模式）

```powershell
cd C:\path\to\DeskNest\TokenNest
npm install
copy .env.example .env
npm start
```

- 透明 console，按 Ctrl-C 停
- 日志直接打到 console
- 文件：`scripts/start.ps1` 一键做这个并自动 copy 配置模板

---

## B. 开机自启（推荐的本机常驻模式，用 NSSM）

### 前置

1. 手动下载 **NSSM**（the Windows Service Helper）：<https://nssm.cc/download>
2. 解压后把 `nssm.exe`（`win64/`）放到 PATH 里的某个目录，比如 `C:\Windows\System32\` 或 `C:\nssm\`
3. 确认 `nssm.exe` 在 PATH 里能找：
   ```powershell
   nssm --version
   ```

### 安装

```powershell
cd C:\path\to\DeskNest\TokenNest
powershell -ExecutionPolicy Bypass -File scripts\install-service.ps1
```

这个脚本会：

1. `npm install`（如果没装过）
2. 用 NSSM 注册 `TokenNest` 服务
3. 设置：启动方式 `SERVICE_AUTO_START`、日志 `logs\tokennest.log`、rotate 1MB
4. 立刻 `start` 服务

### 验证

```powershell
nssm status TokenNest        # 应该是 SERVICE_RUNNING
curl http://127.0.0.1:8787/healthz
```

NSSM 服务使用的 Windows 账户必须能读取该账户自己的
`%USERPROFILE%\.codex\auth.json`。交互式用户执行 `codex login` 成功，不能证明另一
个服务账户能够看到同一认证文件；若服务持续 auth 失败，应先核对服务登录账户。

### 卸载

```powershell
nssm stop TokenNest
nssm remove TokenNest confirm
```

---

## C. 纯手动（不用 NSSM）

把 `npm start` 放到 Windows 任务计划程序的“登录时启动”里，触发器选“登录”，操作选 `npm start`，起始位置设为实际的 `DeskNest\TokenNest` 目录。

> 这种方式无进程监控（崩了不会自启），不推荐生产用。

---

## D. 配置覆盖

`config\tokennest.yaml` 是主配置，项目根目录 `.env` 会由 TokenNest 在 Node 18+
启动时自动加载。优先级：系统/NSSM 环境变量 > `.env` > `yaml` > 内置默认。

what2eat sync/ack 不再使用设备鉴权 token；管理 API 仍由 loopback 或
`TN_WHAT2EAT_ADMIN_TOKEN` 保护。服务与开发板必须位于可信的本地/LAN 边界内。

```powershell
# 例：把端口改成 9000
$env:TN_PORT=9000; npm start
```

完整 env 列表见 `.env.example`。

---

## E. 开机自启后的访问

- 同机器：`http://127.0.0.1:8787/status.json`
- 同 WiFi 的 K10：`http://<本机内网IP>:8787/status.json`（确保防火墙放行 8787）
  - 可选的 Windows 防火墙命令需要在管理员 PowerShell 中运行：`netsh advfirewall firewall add rule name="TokenNest 8787" dir=in action=allow protocol=TCP localport=8787`
  - 或在"高级安全 Windows 防火墙"里手动加一条入站规则
- 找本机内网 IP：`ipconfig` 查 IPv4

---

## F. 日志

- 临时启动：直接 console
- NSSM 模式：`logs\tokennest.log`（安装脚本启用文件轮转，阈值 1MB；当前脚本没有声明固定保留数量）

调试模式：

```powershell
$env:TN_DEBUG=1; npm start
```

`[D][TOK] ... DBG ...` 行会显示 HTTP 请求路径。
