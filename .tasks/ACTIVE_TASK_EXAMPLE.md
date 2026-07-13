# Active Task — Human Draft Template

> 这是第一阶段的人类草案模板，不要求用户填写完整技术方案。
> 第二阶段由 Agent 根据仓库实际内容补充，并等待用户复核后才能进入实施。

## Step 1 — Human Draft

### Task Profile

#### Task Level

请选择一个：

- [ ] **L0 — 小修复**：局部 Bug、参数、文字或低风险调整
- [ ] **L1 — 标准任务**：页面、模块、交互或有限范围功能开发
- [ ] **L2 — 架构任务**：状态机、数据流、核心架构或高回归风险重构

#### Model Thinking Level

请选择智能体在第一阶段之后投入的思考深度。它与任务等级独立：

- [ ] **轻量**：复用已有上下文，快速给出最小可行方案
- [ ] **标准**：检查仓库结构、相关模块、风险和 2～3 个方案
- [ ] **深入**：扩展历史决策、跨模块依赖、替代方案和回归边界

### Task Overview

用一两句话说明想解决什么问题，或想得到什么结果。

### Task Description

可以使用自然语言描述当前想法、现象、目标、参考对象或期望效果。

不需要提前知道：

- 应该修改哪些文件；
- 应该如何设计接口；
- 应该选择什么测试；
- 是否需要新增数据字段；
- 是否需要调用其他 Agent。

### Task Preferences

可以填写偏好的方向，也可以留空：

- 是否优先最小改动：
- 是否优先板端表现：
- 是否允许新增模块：
- 是否保留现有交互：
- 是否偏好某种视觉/技术方案：
- 其他个人偏好：

### User Known Constraints

用户明确知道的限制、不可改变行为或必须保留的内容。未知内容可以留空。

### User Unknowns

用户希望 Agent 帮助判断的部分，例如：

- 不确定任务属于 L0、L1 还是 L2；
- 不知道应该修改哪个模块；
- 不知道有哪些可行方案；
- 不知道应该如何验收；
- 不确定是否会影响现有功能。

## Step 2 — Agent Refinement / User Review

> 以下内容由 Agent 根据全局规则、项目规则、控制文件、当前仓库和历史记录补充。
> Agent 必须说明推断依据；用户确认前不得进入 Implement。

### Repository Context

- 当前相关模块：
- 当前入口和数据流：
- 相关历史决策：
- 相关 CASELOG：
- 当前工作树状态：

### Proposed Task Classification

- 建议任务等级：
- 建议模型思考等级：
- 建议 Resource Mode：
- 判断理由：

### Proposed Scope

- 建议允许修改：
- 建议明确排除：
- 可能需要升级任务等级的条件：

### Proposed Options

#### Option A

- 方案：
- 优点：
- 风险：

#### Option B

- 方案：
- 优点：
- 风险：

#### Option C（可选）

- 方案：
- 优点：
- 风险：

### Proposed Acceptance

- [ ] 功能行为符合预期
- [ ] 原有功能无回归
- [ ] 相关主机测试通过
- [ ] 固件或目标产物验证通过
- [ ] 必要时完成板端/视觉/用户体验验证
- [ ] 相关文档和 Git checkpoint 已更新

### Open Questions

Agent 必须把无法从仓库确定的问题列出，并给出可选择的答案，而不是静默猜测。

1. 问题：
   - [ ] 方案 A
   - [ ] 方案 B
   - [ ] 采用 Agent 推荐

2. 问题：
   - [ ] 方案 A
   - [ ] 方案 B
   - [ ] 采用 Agent 推荐

### Agent Recommendation

推荐方案及理由：

### User Review Decision

- Status: `PENDING_USER_REVIEW`
- 用户选择：
- 用户补充：
- 用户确认日期：

## Confirmed Contract

用户完成第二阶段复核后，由 Agent 将已确认的目标、范围、非目标、验收和证据计划
整理到这里。确认前不得进入代码 Implement。

## Execution State

确认后再使用：

- [ ] Observe
- [ ] Plan
- [ ] Implement
- [ ] Verify
- [ ] Review

## Git Checkpoints

只为有实际产物的阶段创建提交，例如：

```text
[L1][Plan] docs: confirm task scope
[L1][Implement] feat(module): implement accepted option
[L1][Verify] test(module): record verification evidence
```

每个提交使用 `HALF-Work-Level`、`HALF-Work-Stage` 和 `HALF-Work-Task` trailers。
