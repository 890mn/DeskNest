# DeskNest UI Contract 草案

> 面向 UI 智能体与后续页面开发者。  
> 目标：UI 层只根据状态与组件数据渲染，不直接读取传感器、手势、网络、状态机或模块单例。

## 1. 边界原则

UI 层的职责：

- 接收一个完整的 `UIScreenModel`；
- 根据 `UIScreenModel.view.page` 分发到对应页面 renderer；
- 维护必要的绘制缓存、脏区刷新和动画缓存；
- 把未来可能出现的触摸/菜单操作转成 `UIIntent`。

UI 层不负责：

- 不直接调用 `g_state.snapshot()`；
- 不直接调用 `g_sensors.*()`；
- 不直接调用 `g_gesture.*()`；
- 不计算 AI 用量、环境舒适度、专注状态、同步策略；
- 不决定页面跳转；
- 不决定低功耗、翻面、横竖屏状态。

## 1.1 当前方向约束

当前阶段不支持运行时横竖屏动态切换。

原因：K10 / UNIHIKER K10 底层屏幕封装更适合在初始化时选择方向，运行时动态切换会牵涉屏幕初始化、坐标系、缓存、脏区刷新和页面布局整体重排，当前不纳入重构目标。

当前约定：

- 默认运行方向为竖屏；
- 状态机不处理 `GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE` / `GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT` 的页面切换；
- `ORIENTATION_LANDSCAPE`、`SCREEN_LANDSCAPE_*`、`PAGE_LANDSCAPE_*` 暂时作为保留枚举；
- 横屏相关 props 只用于后期“横屏初始化版本”或单独适配分支；
- UI 智能体当前只需要保证竖屏页面、face-down 页和配置页的视觉质量。

后期如果要做横屏，只按“启动时选择横屏布局”处理，而不是运行中动态旋转。

数据流固定为：

```text
input / sensors / modules
  -> AppEvent
  -> StateMachine
  -> AppState
  -> Module snapshots
  -> ViewModelBuilder
  -> UIScreenModel
  -> UI renderer
```

## 2. 核心枚举

现有枚举暂时保留在 `src/config.h` 中。后续可以移动到 `src/core/` 或 `src/view/`，但语义应保持稳定。

### SystemState

```cpp
enum SystemState : uint8_t {
    SYSTEM_BOOT = 0,
    SYSTEM_ACTIVE,
    SYSTEM_AMBIENT,
    SYSTEM_LIGHT_SLEEP,
    SYSTEM_FACE_DOWN_SLEEP,
    SYSTEM_CONFIG,
};
```

### OrientationState

```cpp
enum OrientationState : uint8_t {
    ORIENTATION_UNKNOWN = 0,
    ORIENTATION_PORTRAIT,
    ORIENTATION_LANDSCAPE,
    ORIENTATION_FACE_DOWN,
};
```

说明：当前阶段 `ORIENTATION_LANDSCAPE` 只作为未来初始化方向适配的保留值，不参与运行时页面跳转。

### ScreenMode

`ScreenMode` 表示当前屏幕布局/交互模式，不等同于具体页面。当前阶段只使用竖屏、face-down 和 config 相关模式；横屏模式保留给后期“启动时横屏初始化适配”。

```cpp
enum ScreenMode : uint8_t {
    SCREEN_PORTRAIT_OVERVIEW = 0,
    SCREEN_PORTRAIT_DETAIL,
    SCREEN_LANDSCAPE_OVERVIEW,
    SCREEN_LANDSCAPE_FOCUS,
    SCREEN_LANDSCAPE_CUSTOM,
    SCREEN_FACE_DOWN,
    SCREEN_CONFIG,
};
```

### UIPage

```cpp
enum UIPage : uint8_t {
    PAGE_PORTRAIT_OVERVIEW = 0,
    PAGE_PORTRAIT_AI_USAGE,
    PAGE_PORTRAIT_ENVIRONMENT,
    PAGE_PORTRAIT_SETTINGS,
    PAGE_LANDSCAPE_OVERVIEW,
    PAGE_LANDSCAPE_FOCUS,
    PAGE_LANDSCAPE_CUSTOM,
    PAGE_SLEEP_FACE_DOWN,
    PAGE_CONFIG_PORTAL,
    PAGE_COUNT,
};
```

## 3. 顶层模型：UIScreenModel

UI 每一帧只消费这个模型。

```cpp
struct UIScreenModel {
    ViewState view;
    HeaderProps header;
    FooterProps footer;
    NavigationProps nav;
    StatusProps status;
    PageProps page;
    AnimationProps animation;
};
```

## 4. ViewState

```cpp
struct ViewState {
    ScreenMode mode;
    UIPage page;
    OrientationState orientation;
    SystemState system;

    bool isAmbient;
    bool isFaceDown;
    bool isSleeping;
    bool isConfig;

    uint32_t nowMs;
    uint32_t idleSeconds;
};
```

UI 可以根据这些字段决定：

- 是否画页码；
- 是否画 footer；
- 是否降低动画频率；
- 是否进入 face-down 极简页。

UI 不应该根据这些字段自行改变页面。

## 5. HeaderProps

```cpp
struct HeaderProps {
    const char* title;
    const char* subtitle;

    uint8_t pageIndex;
    uint8_t pageCount;

    bool showHeader;
    bool showPageDots;
};
```

约定：

- `pageIndex` 从 0 开始；
- UI 可以显示为 `P{pageIndex + 1}/{pageCount}`；
- face-down 页通常 `showHeader = false`。

## 6. FooterProps

```cpp
struct FooterProps {
    const char* leftHint;
    const char* rightHint;
    const char* statusText;

    bool showFooter;
    bool showIdle;
};
```

示例：

```text
leftHint:  "[A] Next"
rightHint: "[B] Prev"
statusText: "idle 12s"
```

## 7. NavigationProps

```cpp
struct NavigationProps {
    bool canNext;
    bool canPrev;
    bool canSelect;
    bool canBack;

    const char* nextLabel;
    const char* prevLabel;
    const char* selectLabel;
    const char* backLabel;
};
```

UI 只显示导航能力，不执行导航。实际跳转由状态机处理。

## 8. StatusProps

```cpp
struct StatusProps {
    const char* systemText;
    const char* orientationText;
    const char* wifiText;
    const char* syncText;

    uint8_t batteryPercent;
    bool batteryValid;
    bool charging;
    bool warning;
};
```

## 9. PageProps

```cpp
enum PagePropsType : uint8_t {
    PAGE_PROPS_NONE = 0,
    PAGE_PROPS_PORTRAIT_OVERVIEW,
    PAGE_PROPS_AI_USAGE,
    PAGE_PROPS_ENVIRONMENT,
    PAGE_PROPS_SETTINGS,
    PAGE_PROPS_LANDSCAPE_OVERVIEW,
    PAGE_PROPS_FOCUS,
    PAGE_PROPS_CUSTOM,
    PAGE_PROPS_FACE_DOWN,
    PAGE_PROPS_CONFIG,
};

struct PageProps {
    PagePropsType type;

    PortraitOverviewProps portraitOverview;
    AiUsageProps aiUsage;
    EnvironmentProps environment;
    SettingsProps settings;
    LandscapeOverviewProps landscapeOverview;
    FocusProps focus;
    CustomProps customPage;
    FaceDownProps faceDown;
    ConfigProps config;
};
```

嵌入式实现阶段可以把这个 union 化，避免同时占用所有页面 props 的内存。文档先用直观结构表达契约。

## 10. 页面 props

### PortraitOverviewProps

```cpp
struct PortraitOverviewProps {
    const char* timeText;

    uint8_t aiTotalPercent;
    const char* aiStatusText;

    float temperatureC;
    float humidityPct;
    uint16_t lux;
    bool environmentValid;

    const char* comfortText;
    const char* suggestionText;
    const char* messageText;
    const char* updatedAtText;
};
```

### AiUsageProps

```cpp
struct UsageItemProps {
    const char* name;
    uint8_t percent;
    uint8_t weeklyPercent;
    bool fiveHourAvailable;
    bool weeklyAvailable;
    uint8_t effectivePercent;
    const char* statusText;
    const char* detailText;
    const char* fiveHourExpireAt;
    const char* weekExpireAt;
};

struct AiUsageProps {
    uint8_t totalPercent;
    UsageItemProps minimax;
    UsageItemProps codex;
    UsageItemProps chatgpt;

    const char* updatedAtText;
    const char* warningText;
};
```

`effectivePercent` 是展示层使用的统一百分比：5h 窗口存在时取 `percent`，否则
在 weekly 窗口存在时取 `weeklyPercent`。`fiveHourAvailable` 和
`weeklyAvailable` 是窗口存在性的显式信号；`percent == 0` 或缺少重置时间不能
单独推断为“无限额度”。AI 详情页可据此在“没有 5h 但有 weekly”时显示
`NO LIMIT`，在未知或失败时显示未知状态。

### EnvironmentProps

```cpp
struct EnvironmentProps {
    float temperatureC;
    float humidityPct;
    uint16_t lux;
    bool valid;

    uint8_t score;
    const char* gradeText;
    const char* temperatureGrade;
    const char* humidityGrade;
    const char* lightGrade;
    const char* adviceText;
};
```

### SettingsProps

```cpp
struct SettingRowProps {
    const char* label;
    const char* value;
    bool selectable;
};

struct SettingsProps {
    SettingRowProps rows[8];
    uint8_t rowCount;
    uint8_t selectedIndex;
    const char* dangerHint;
};
```

### LandscapeOverviewProps

当前阶段保留此 props，但 UI 智能体不需要优先实现或维护横屏页面。它只用于后期横屏初始化适配。

```cpp
struct LandscapeOverviewProps {
    uint8_t aiTotalPercent;
    const char* minimaxText;
    const char* codexText;

    float temperatureC;
    float humidityPct;
    uint16_t lux;
    bool environmentValid;

    const char* systemText;
};
```

### FocusProps

```cpp
enum FocusState : uint8_t {
    FOCUS_IDLE = 0,
    FOCUS_RUNNING,
    FOCUS_PAUSED,
    FOCUS_DONE,
};

struct FocusProps {
    const char* modeText;
    const char* timerText;
    FocusState state;
    const char* stateText;
    const char* goalText;
    uint8_t aiTotalPercent;
    const char* environmentText;
};
```

### CustomProps

```cpp
struct CustomCardProps {
    const char* label;
    const char* value;
    bool active;
};

struct CustomProps {
    CustomCardProps cards[4];
    uint8_t cardCount;
    const char* hintText;
};
```

### FaceDownProps

```cpp
struct FaceDownProps {
    const char* line1;
    const char* line2;
    const char* line3;
    bool showBreathingDot;
};
```

### ConfigProps

```cpp
struct ConfigProps {
    const char* ssidText;
    const char* urlText;
    const char* stepText;
    const char* hintText;
};
```

## 11. AnimationProps

```cpp
enum ShakeVisualPhase : uint8_t {
    SHAKE_VISUAL_IDLE = 0,
    SHAKE_VISUAL_OUTBOUND,
    SHAKE_VISUAL_RETURNING,
};

struct AnimationProps {
    ShakeVisualPhase shakePhase;
    int8_t shakeDirection;     // -1 = right, +1 = left, 0 = none
    uint8_t shakeProgressPct;

    bool pageChanged;
    bool forceFullRedraw;
};
```

后续 UI 层可以用这个字段画轻量动画，但动画阶段由 ViewModelBuilder 从输入层快照映射，不由 UI 直接读取 `g_gesture`。

## 12. UIIntent

当前 K10 主要由按键/手势驱动，UI 暂时可以不发 intent。为未来触摸菜单预留：

```cpp
enum UIIntent : uint8_t {
    UI_INTENT_NONE = 0,
    UI_INTENT_NEXT,
    UI_INTENT_PREV,
    UI_INTENT_SELECT,
    UI_INTENT_BACK,
    UI_INTENT_MENU,
    UI_INTENT_REFRESH,
};
```

UI intent 的处理路径：

```text
UIIntent
  -> AppEvent
  -> StateMachine
  -> AppState
  -> ViewModelBuilder
  -> UIScreenModel
```

## 13. 页面渲染函数约定

每个页面 renderer 的目标签名：

```cpp
bool renderPortraitOverview(const UIScreenModel& model, RenderCache& cache);
bool renderAiUsage(const UIScreenModel& model, RenderCache& cache);
bool renderEnvironment(const UIScreenModel& model, RenderCache& cache);
bool renderFocus(const UIScreenModel& model, RenderCache& cache);
```

返回值：

- `true`：画布有变化，需要 `updateCanvas()`；
- `false`：无需刷新。

## 14. 渐进迁移约定

第一阶段允许 UI 内部继续复用旧绘制代码，但必须逐步替换这些直接依赖：

| 旧依赖 | 替换为 |
| --- | --- |
| `g_state.snapshot()` | `model.view` / `model.status` |
| `g_sensors.aht20()` | `model.page.*` |
| `g_sensors.ltr303()` | `model.page.*` |
| `g_sensors.battery()` | `model.status` |
| `g_gesture.shakePhase()` | `model.animation` |
| UI 内 mock AI | `AiUsageModule` 或 `ViewModelBuilder` |
| UI 内环境评分 | `EnvironmentModule` 或 `ViewModelBuilder` |
| UI 内页面跳转判断 | `StateMachine` / `PageRegistry` |

当前阶段迁移优先级：

1. 竖屏总览；
2. AI 用量页；
3. 环境页；
4. 设置页；
5. face-down 栖息页；
6. config portal；
7. 横屏初始化适配，后期再做。

## 15. 完成定义

当满足以下条件时，可以认为 UI 层和架构层完成第一轮解耦：

- `dn_ui_render()` 不再直接读取 `g_state`、`g_sensors`、`g_gesture`；
- UI 页面 renderer 只接收 `UIScreenModel`；
- 页面切换只发生在状态机；
- AI、环境、专注、系统状态数据由模块或 ViewModelBuilder 提供；
- 新增一个竖屏页面只需要注册页面定义、提供 props、增加 renderer，不需要改多处核心逻辑；
- 运行时横竖屏动态切换不属于本轮完成标准。
