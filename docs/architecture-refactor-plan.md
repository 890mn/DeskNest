# DeskNest 架构重构计划草案

> 范围：状态机、数据流、事件系统、模块接口、UI Contract。  
> 原则：渐进式重构，每一步后保持项目可编译、可运行，不一次性推倒。

## 1. 当前项目读取结论

当前主循环位于 `src/app.cpp`，基本流程是：

```text
Sensors.update()
  -> GestureEngine.update()
  -> gesture tuning post step
  -> button poll
  -> StateMachine.update()
  -> dn_ui_render()
  -> heartbeat/debug output
```

已有的正向资产：

- `GestureEvent`、`ButtonEvent` 已有统一枚举；
- `StateMachine` 已有独立文件；
- `StateSnapshot` 已包含 system、face、orientation、page、rotation lock；
- 翻面状态已被显式建模为 `FaceSubState`；
- 横竖屏页面记忆已经出现；
- 手势识别已有 native 测试；
- UI 已经有页面级 dispatch 和局部刷新缓存。

主要问题：

1. UI 直接读取状态机、传感器和手势单例；
2. UI 内混入 mock AI、环境评分、专注页状态和自定义页轮换；
3. 页面循环散落在 `nextPortrait/nextLandscape` switch 中；
4. `GestureEvent`、`OrientationState detected` 同时参与旋转转移，且当前硬件封装不适合运行时动态旋转；
5. 翻面状态由 `system/face/orientation/page` 多字段共同表达，需要明确主事实源；
6. 数据刷新策略尚未模块化；
7. 新增页面需要改 enum、状态机、UI cache、render dispatch 等多处；
8. 状态机缺少 host 侧回归测试。

## 1.1 新确认的方向约束

当前 MVP 不做运行时横竖屏动态切换。

原因：

- K10 / UNIHIKER K10 的屏幕封装更适合初始化时设置方向；
- 运行时横竖屏切换会牵涉 canvas 初始化、坐标变换、脏区缓存、页面布局和输入语义重算；
- 为了保持架构轻量，本轮不为硬件底层暂时不好支持的能力设计复杂状态。

当前策略：

- 默认竖屏启动和运行；
- face-down / face-up 保留为核心姿态交互；
- 左摇 / 右摇只在当前竖屏页面组内切换模块；
- 横屏相关 enum、props、页面文件可保留，但视为后期“横屏初始化适配”的占位；
- 状态机当前不处理运行时 `ROTATE_PORTRAIT_TO_LANDSCAPE` / `ROTATE_LANDSCAPE_TO_PORTRAIT` 页面切换；
- 后期如果要支持横屏，应作为独立启动模式：开机读取配置，初始化屏幕方向，再使用一套横屏页面 registry。

## 2. 重构目标

一句话目标：

> 输入统一成事件，事件只改 AppState，模块只产数据，UI 只消费 ViewModel。

具体目标：

- `AppState` 是全局应用状态的单一事实源；
- `StateMachine` 是唯一修改页面、模式、系统状态的地方；
- `GestureEvent`、`ButtonEvent`、face 状态、power tick 进入统一事件流；
- `Module` 负责数据刷新与业务计算；
- `ViewModelBuilder` 把 `AppState + Module snapshots` 组装成 `UIScreenModel`；
- UI renderer 只消费 `UIScreenModel`；
- 新增竖屏页面优先改注册表，不大改核心状态机；
- 适配 K10 小屏幕和 MCU 环境，避免复杂框架、堆内存和过度抽象。

## 3. 推荐目录结构

最终目标结构如下。迁移不要求一次完成。

```text
src/
  app.h
  app.cpp
  config.h

  core/
    app_state.h
    app_events.h
    state_machine.h
    state_machine.cpp

  input/
    gesture.h
    gesture.cpp
    gesture_tuning.h
    gesture_tuning.cpp
    buttons.h
    buttons.cpp
    input_dispatcher.h
    input_dispatcher.cpp

  platform/
    sensors.h
    sensors.cpp
    k10_device.h
    k10_device.cpp

  modules/
    module.h
    environment_module.h
    environment_module.cpp
    ai_usage_module.h
    ai_usage_module.cpp
    focus_module.h
    focus_module.cpp
    system_status_module.h
    system_status_module.cpp

  view/
    screen_mode.h
    page_registry.h
    page_registry.cpp
    view_model.h
    view_model_builder.h
    view_model_builder.cpp
    ui_contract.h

  ui/
    ui.h
    ui.cpp
    ui_renderer.h
    ui_renderer.cpp
    pages/
      page_common.h
      portrait_overview.cpp
      portrait_ai_usage.cpp
      portrait_environment.cpp
      portrait_settings.cpp
      landscape_overview.cpp      # reserved: future boot-time landscape mode
      landscape_focus.cpp         # reserved: future boot-time landscape mode
      landscape_custom.cpp        # reserved: future boot-time landscape mode
      face_down.cpp
      config_portal.cpp
```

过渡期可以先保持当前 `src/` 平铺结构，只新增 `view_model.*` 与 `ui_contract.h`。目录移动放在解耦稳定之后。

## 4. AppState 草案

```cpp
struct AppState {
    SystemState system;
    FaceSubState face;
    OrientationState orientation;
    ScreenMode screenMode;
    UIPage currentPage;
    RotationLock rotationLock;

    uint32_t nowMs;
    uint32_t lastInputMs;
    uint32_t lastModeChangeMs;

    UIPage lastPortraitPage;
    UIPage lastLandscapePage;     // reserved: future boot-time landscape mode
    UIPage preFaceDownPage;

    bool screenDirty;
    bool wakeRequested;
};
```

主事实源建议：

- `face` 决定是否处于翻面硬门；
- `system` 决定 active / ambient / sleep / config；
- `orientation` 决定物理姿态；当前只使用 PORTRAIT / FACE_DOWN；
- `screenMode` 决定布局和导航策略；
- `currentPage` 决定当前具体页面。

## 5. 统一事件草案

```cpp
enum AppEventType : uint8_t {
    APP_EVENT_NONE = 0,
    APP_EVENT_GESTURE,
    APP_EVENT_BUTTON,
    APP_EVENT_ORIENTATION,   // reserved: future boot-time landscape mode, not runtime rotation
    APP_EVENT_POWER_TICK,
    APP_EVENT_MODULE_UPDATED,
    APP_EVENT_UI_INTENT,
};

struct AppEvent {
    AppEventType type;
    uint32_t atMs;

    GestureEvent gesture;
    ButtonEvent button;
    OrientationState orientation;
    UIIntent uiIntent;
};
```

事件处理优先级：

```text
FACE_DOWN / FACE_UP
  > POWER_WAKE / POWER_SLEEP
  > BUTTON
  > GESTURE_NAVIGATION
  > MODULE_UPDATED

说明：运行时 ORIENTATION_CHANGE 暂不参与页面状态迁移。
```

## 6. 状态机草案

### 启动

```text
BOOT
  -> init sensors/input/modules/ui
  -> system = ACTIVE
  -> face = UP
  -> orientation = PORTRAIT
  -> screenMode = SCREEN_PORTRAIT_OVERVIEW
  -> currentPage = PAGE_PORTRAIT_OVERVIEW
```

### 竖屏导航

```text
PORTRAIT + SHAKE_LEFT / BUTTON_PREV
  -> previous page in portrait group

PORTRAIT + SHAKE_RIGHT / BUTTON_NEXT
  -> next page in portrait group

PORTRAIT + BUTTON_BACK
  -> PAGE_PORTRAIT_OVERVIEW
```

### 横屏初始化适配，后期保留

当前不做运行时竖转横 / 横转竖。

后期如需横屏，只做启动时选择：

```text
BOOT
  -> read preferredDisplayOrientation
  -> init screen as PORTRAIT or LANDSCAPE
  -> choose portrait registry or landscape registry
  -> enter default page for that registry
```

这意味着：

- `GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE` 当前不改变页面；
- `GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT` 当前不改变页面；
- `lastLandscapePage` 保留但暂不使用；
- 横屏页面不进入当前 MVP 的状态机测试范围。

### 翻面

```text
FACE_UP + GESTURE_FACE_DOWN
  -> save preFaceDownPage
  -> face = DOWN
  -> orientation = FACE_DOWN
  -> system = FACE_DOWN_SLEEP
  -> screenMode = SCREEN_FACE_DOWN
  -> currentPage = PAGE_SLEEP_FACE_DOWN
```

翻面硬门：

```text
FACE_STATE_DOWN:
  ignore SHAKE_LEFT
  ignore SHAKE_RIGHT
  ignore BUTTON_NEXT/PREV/SELECT/BACK/MENU
  allow FACE_UP_OPEN
  optionally allow BUTTON_FACTORY
```

### 翻面恢复

产品决策：恢复翻面前页面，保留桌面物件的连续性。

```text
FACE_STATE_DOWN + GESTURE_FACE_UP_OPEN
  -> face = UP
  -> system = ACTIVE
  -> restore currentPage = preFaceDownPage
  -> restore orientation from page group or detected orientation
  -> mark screen full redraw
  -> show cached module data first
```

## 7. PageRegistry 草案

```cpp
struct PageDef {
    UIPage id;
    ScreenMode mode;
    ModuleId primaryModule;
    const char* title;
    uint8_t group;
    bool showInPageDots;
};
```

页面组：

```cpp
enum PageGroup : uint8_t {
    PAGE_GROUP_PORTRAIT = 0,
    PAGE_GROUP_LANDSCAPE,  // reserved: future boot-time landscape mode
    PAGE_GROUP_SPECIAL,
};
```

初始注册表：

```cpp
static const PageDef PAGE_REGISTRY[] = {
    { PAGE_PORTRAIT_OVERVIEW,    SCREEN_PORTRAIT_OVERVIEW,  MODULE_SYSTEM_STATUS, "DeskNest",    PAGE_GROUP_PORTRAIT, true  },
    { PAGE_PORTRAIT_AI_USAGE,    SCREEN_PORTRAIT_DETAIL,    MODULE_AI_USAGE,      "AI Usage",    PAGE_GROUP_PORTRAIT, true  },
    { PAGE_PORTRAIT_ENVIRONMENT, SCREEN_PORTRAIT_DETAIL,    MODULE_ENVIRONMENT,   "Environment", PAGE_GROUP_PORTRAIT, true  },
    { PAGE_PORTRAIT_SETTINGS,    SCREEN_PORTRAIT_DETAIL,    MODULE_SYSTEM_STATUS, "Settings",    PAGE_GROUP_PORTRAIT, true  },
    // Reserved for future boot-time landscape initialization mode:
    { PAGE_LANDSCAPE_OVERVIEW,   SCREEN_LANDSCAPE_OVERVIEW, MODULE_SYSTEM_STATUS, "DeskNest",    PAGE_GROUP_LANDSCAPE, true },
    { PAGE_LANDSCAPE_FOCUS,      SCREEN_LANDSCAPE_FOCUS,    MODULE_FOCUS,         "Focus",       PAGE_GROUP_LANDSCAPE, true },
    { PAGE_LANDSCAPE_CUSTOM,     SCREEN_LANDSCAPE_CUSTOM,   MODULE_SYSTEM_STATUS, "Custom",      PAGE_GROUP_LANDSCAPE, true },
    { PAGE_SLEEP_FACE_DOWN,      SCREEN_FACE_DOWN,          MODULE_SYSTEM_STATUS, "Roost",       PAGE_GROUP_SPECIAL,   false },
    { PAGE_CONFIG_PORTAL,        SCREEN_CONFIG,             MODULE_SYSTEM_STATUS, "WiFi Setup",  PAGE_GROUP_SPECIAL,   false },
};
```

后续 `nextPage/prevPage` 从注册表查找，不再由状态机硬编码 switch。

## 8. Module 草案

```cpp
enum ModuleId : uint8_t {
    MODULE_TIME = 0,
    MODULE_AI_USAGE,
    MODULE_ENVIRONMENT,
    MODULE_FOCUS,
    MODULE_SYSTEM_STATUS,
    MODULE_WEATHER,
    MODULE_SCHEDULE,
};

struct ModuleSnapshot {
    ModuleId id;
    bool valid;
    uint32_t updatedAtMs;
    uint32_t nextRefreshMs;
};
```

模块职责：

- Time：当前时间、日期、格式化文本；
- AI Usage：MiniMax / Codex / ChatGPT 用量与同步状态；
- Environment：温湿度、光照、舒适度评分与建议；
- Focus：番茄钟/专注状态；
- System Status：WiFi、电池、系统状态、同步提示；
- Weather / Schedule：后续扩展。

## 9. 渐进式执行步骤

### Step 1：文档与契约

- 创建 `docs/UI_CONTRACT.md`；
- 创建 `docs/architecture-refactor-plan.md`；
- 不改业务代码。

### Step 2：ViewModel 类型落地

- 新增 `src/view_model.h` 或 `src/view/view_model.h`；
- 定义 `UIScreenModel`、页面 props、动画 props；
- 不接入 UI。

### Step 3：ViewModelBuilder

- 新增 `view_model_builder.*`；
- 暂时从现有 `g_state/g_sensors/g_gesture` 读数据；
- 输出 `UIScreenModel`；
- 保证旧 UI 仍然可运行。

### Step 4：UI 渲染入口解耦

- 将 `dn_ui_render()` 改成：

```cpp
UIScreenModel model = buildUIScreenModel(...);
dn_ui_render_model(model);
```

- 页面 renderer 逐个从全局读取迁移到 `model.page.*`。

### Step 5：业务计算迁出 UI

- AI mock 从 UI 移到 AI module 或 ViewModelBuilder；
- 环境评分从 UI 移到 Environment module；
- Focus 固定状态从 UI 移到 Focus module；
- Shake 动画状态从 `g_gesture` 映射到 `AnimationProps`。

### Step 6：PageRegistry

- 引入页面注册表；
- 替换 `nextPortrait/prevPortrait` 的硬编码 switch；
- `nextLandscape/prevLandscape` 暂时保留或标记为 reserved，不进入当前运行路径；
- 新增竖屏页面只改注册表。

### Step 7：状态机测试

新增 host 测试覆盖：

- 默认启动状态；
- 竖屏左右切页；
- 翻面进入 sleep；
- 翻面状态屏蔽普通输入；
- 翻回恢复 preFaceDownPage；
- 按键 back/menu/factory。

明确排除：

- 运行时横屏左右切页；
- 运行时竖转横；
- 运行时横转竖。

### Step 8：目录整理

在行为稳定后，再移动到 `core/input/modules/view/ui/platform` 目录结构。目录移动应单独提交，避免和逻辑改动混在一起。

## 10. 解耦完成检查表

当以下条件满足时，可以通知用户“架构和 UI 层已经完成第一轮解耦”：

- `dn_ui_render()` 不直接读取 `g_state`；
- 页面 renderer 不直接读取 `g_sensors`；
- 页面 renderer 不直接读取 `g_gesture`；
- AI usage 不在 UI 内 mock；
- 环境舒适度不在 UI 内计算；
- 页面跳转不在 UI 内决定；
- 竖屏页面分组与标题来自 `PageRegistry`；
- 状态机核心路径有测试；
- 新增一个竖屏页面不需要改超过三处核心文件；
- 运行时横竖屏动态切换没有进入当前状态机和 UI renderer 的核心路径。
