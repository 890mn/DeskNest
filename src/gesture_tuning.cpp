// src/gesture_tuning.cpp
// 栖屏 DeskNest - 手势调参串口 REPL + 校准 wizard
// "栖于桌面，息于常亮之间"
//
// 两个层：
//   1) 直接命令：show / set / reset / record / feed / verbose —— 任何时候可用
//   2) 校准 wizard：test 进入 → 按步骤引导用户做手势 → ENTER 录 → 反馈
//                   → set 调参 → next/prev/skip/redo/exit 切换
//
// 设计目标：用户串口敲一敲就能把真机阈值调到合理值，不需要重烧固件。
// 调好之后写回 src/config.h 重烧（受 NVS 持久化之前，编译期常量最稳）。

#include "gesture_tuning.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

namespace desknest {

// ---------------------------------------------------------------------------
// 行缓冲（用户敲到 '\n' 才解析）
// 不用 LINE_MAX —— 会被 xtensa 工具链 <limits.h> 里的同名宏撞掉
// ---------------------------------------------------------------------------
static constexpr uint8_t TUNING_LINE_MAX = 96;
static char     g_line[TUNING_LINE_MAX];
static uint8_t  g_line_len = 0;

// 注入 accel 队列（最近一次 feed 替换真实 accel 一帧）
static bool           g_pending_feed = false;
static AccelReading   g_pending_feed_acc = { false, 0, 0, 0, 0 };

// recording 流（accel + event 写到串口）
static bool     g_recording = false;
static uint32_t g_record_last_ms = 0;
static constexpr uint32_t RECORD_PERIOD_MS = 50;   // 20 Hz

// ---------------------------------------------------------------------------
// 校准 wizard 状态
// ---------------------------------------------------------------------------
enum TestState : uint8_t {
    TEST_OFF = 0,         // 不在校准
    TEST_PROMPT,          // 等待用户：ENTER 开始录制 / set 调 / next 等
    TEST_CAPTURING,       // 正在录数据；超时 / 期望事件触发就跳到 FEEDBACK
    TEST_FEEDBACK,        // 已显示结果；等待用户决定
};

struct GestureStep {
    const char*    name;         // 显示名
    GestureEvent   expected;     // 这一步应触发的事件
    uint16_t       max_ms;       // 录制最长窗口（超时未触发就进 FEEDBACK）
    const char*    instruction;  // 给用户的中文提示
    const char*    tunables[7];  // 此步可调的字段名
    uint8_t        num_tunables;
};

static const GestureStep STEPS[] = {
    { "face_down",
      GESTURE_FACE_DOWN,
      2000,
      "把设备面朝下放在桌面，按住 1-2 秒",
      { "face_down", "face_dn_ms", "face_cd" }, 3 },

    { "face_up",
      GESTURE_FACE_UP_OPEN,
      2000,
      "把设备翻回正面朝上，按住 1-2 秒",
      { "face_up", "face_up_ms", "face_cd" }, 3 },

    { "rotate_p2l",
      GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE,
      2500,
      "顺时针旋转 90°（从竖屏到横屏）",
      { "rotate", "rotate_amb", "rotate_ms", "rotate_cd" }, 4 },

    { "rotate_l2p",
      GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT,
      2500,
      "逆时针旋转 90°（从横屏回竖屏）",
      { "rotate", "rotate_amb", "rotate_ms", "rotate_cd" }, 4 },

    { "shake_left",
      GESTURE_SHAKE_LEFT,
      2000,
      "向左甩出，再拉回并停稳；方向反了会自动修正",
      { "shake", "shake_return", "shake_settle", "shake_window", "shake_cd", "shake_invert", "shake_fire" }, 7 },

    { "shake_right",
      GESTURE_SHAKE_RIGHT,
      2000,
      "向右甩出，再拉回并停稳；用于复核左右方向",
      { "shake", "shake_return", "shake_settle", "shake_window", "shake_cd", "shake_invert", "shake_fire" }, 7 },

    { "tap",
      GESTURE_TAP,
      1500,
      "用手指轻敲一下屏幕背面",
      { "tap_high", "tap_low", "tap_cd" }, 3 },
};
static const uint8_t NUM_STEPS = sizeof(STEPS) / sizeof(STEPS[0]);

static TestState  g_test_state        = TEST_OFF;
static uint8_t    g_test_step_idx     = 0;
static uint32_t   g_capture_started_ms = 0;

// 录到的统计
static float        g_sum_az = 0,  g_min_az =  1e9f, g_max_az = -1e9f;
static float        g_sum_a  = 0,  g_min_a  =  1e9f, g_max_a  = -1e9f;
static uint32_t     g_samples = 0;
static GestureEvent g_fired     = GESTURE_NONE;
static uint32_t     g_fired_at  = 0;
static float        g_first_ax  = 0;     // 录到的第一条 ax —— shake 方向依据
static bool         g_have_first_ax = false;
static float        g_peak_abs_ax = 0;

// ---------------------------------------------------------------------------
// 字符串工具
// ---------------------------------------------------------------------------

static const char* gestureEventName(GestureEvent e) {
    switch (e) {
        case GESTURE_NONE:                        return "NONE";
        case GESTURE_SHAKE_LEFT:                  return "SHAKE_L";
        case GESTURE_SHAKE_RIGHT:                 return "SHAKE_R";
        case GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE:return "ROT_P2L";
        case GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT:return "ROT_L2P";
        case GESTURE_FACE_DOWN:                   return "FACE_DN";
        case GESTURE_FACE_UP_OPEN:                return "FACE_UP";
        case GESTURE_TAP:                         return "TAP";
    }
    return "?";
}

static const char* orientName(OrientationState o) {
    switch (o) {
        case ORIENTATION_UNKNOWN:   return "UNK";
        case ORIENTATION_PORTRAIT:  return "PORT";
        case ORIENTATION_LANDSCAPE: return "LAND";
        case ORIENTATION_FACE_DOWN: return "FACE_DN";
    }
    return "?";
}

static const char* testStateName(TestState s) {
    switch (s) {
        case TEST_OFF:       return "off";
        case TEST_PROMPT:    return "prompt";
        case TEST_CAPTURING: return "capturing";
        case TEST_FEEDBACK:  return "feedback";
    }
    return "?";
}

static const char* shakePhaseName(ShakePhase phase) {
    switch (phase) {
        case SHAKE_PHASE_OUTBOUND:  return "OUTBOUND";
        case SHAKE_PHASE_RETURNING: return "RETURNING";
        default:                    return "IDLE";
    }
}

static const char* shakeDirectionName(int8_t direction) {
    if (direction > 0) return "LEFT";
    if (direction < 0) return "RIGHT";
    return "NONE";
}

// ---------------------------------------------------------------------------
// 字段反射（set / show 用）
// ---------------------------------------------------------------------------
namespace fieldref {
    enum Kind { F_FLOAT, F_U16, F_U8 };
    struct Ref {
        Kind kind;
        union {
            float*    f;
            uint16_t* u;
            uint8_t*  b;
        } p;
    };
}

static bool findField(const char* name, fieldref::Ref& ref) {
    if (!strcmp(name, "face_down"))   { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.face_down_threshold;   return true; }
    if (!strcmp(name, "face_up"))     { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.face_up_threshold;     return true; }
    if (!strcmp(name, "rotate"))      { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.rotate_threshold;      return true; }
    if (!strcmp(name, "rotate_amb"))  { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.rotate_amb;            return true; }
    if (!strcmp(name, "shake"))       { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.shake_threshold;       return true; }
    if (!strcmp(name, "shake_return")){ ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.shake_return_threshold; return true; }
    if (!strcmp(name, "shake_settle")){ ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.shake_settle_threshold; return true; }
    if (!strcmp(name, "shake_window")){ ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.shake_window_ms;       return true; }
    if (!strcmp(name, "shake_invert")) { ref.kind = fieldref::F_U8;   ref.p.b = &g_tuning.shake_invert;          return true; }
    if (!strcmp(name, "shake_fire")) { ref.kind = fieldref::F_U8;   ref.p.b = &g_tuning.shake_fire_on_outbound; return true; }
    if (!strcmp(name, "shake_en"))    { ref.kind = fieldref::F_U8;   ref.p.b = &g_tuning.gesture_shake_enabled;  return true; }
    if (!strcmp(name, "tap_high"))    { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.tap_z_high;            return true; }
    if (!strcmp(name, "tap_low"))     { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.tap_z_low;             return true; }
    if (!strcmp(name, "face_dn_ms"))  { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.face_down_stable_ms;   return true; }
    if (!strcmp(name, "face_up_ms"))  { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.face_up_stable_ms;     return true; }
    if (!strcmp(name, "face_cd"))     { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.face_cooldown_ms;      return true; }
    if (!strcmp(name, "rotate_ms"))   { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.rotate_stable_ms;      return true; }
    if (!strcmp(name, "rotate_cd"))   { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.rotate_cooldown_ms;    return true; }
    if (!strcmp(name, "shake_cd"))    { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.shake_cooldown_ms;     return true; }
    if (!strcmp(name, "tap_cd"))      { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.tap_cooldown_ms;       return true; }
    if (!strcmp(name, "verbose"))     { ref.kind = fieldref::F_U8;    ref.p.b = &g_tuning.verbose;               return true; }
    return false;
}

// ---------------------------------------------------------------------------
// 输出（prompt / help / show / state）
// ---------------------------------------------------------------------------

static void printPrompt() {
    if (g_test_state == TEST_OFF) {
        Serial.print("g> ");
    } else {
        Serial.printf("[%u/%u %s] g> ",
                      g_test_step_idx + 1, NUM_STEPS,
                      STEPS[g_test_step_idx].name);
    }
}

static void printHelp() {
    Serial.println();
    Serial.println("=== Calibration wizard (主流程) ===");
    Serial.println("  test (t)         进入引导：face_down → face_up → rotate x2 → 左甩 → 右甩 → tap");
    Serial.println("                   每步：ENTER 录数据 → 反馈 → set 调 → next/prev 切");
    Serial.println("  next (n)         下一步");
    Serial.println("  prev (p)         上一步");
    Serial.println("  skip (s)         跳过当前");
    Serial.println("  redo (r)         重做当前（清状态再录）");
    Serial.println("  exit (q)         退出 wizard");
    Serial.println();
    Serial.println("=== Direct knobs (任何时候可用) ===");
    Serial.println("  show             所有当前阈值");
    Serial.println("  set <n> <v>      改阈值（test 模式下只建议改当前步的字段）");
    Serial.println("  reset            全部恢复 config.h 默认");
    Serial.println();
    Serial.println("=== Diagnostics ===");
    Serial.println("  state            orientation / wizard 状态 / pending feed");
    Serial.println("  verbose 0|1      关/开 1Hz 心跳流（默认 0 = 安静）");
    Serial.println("  record           20Hz 流 accel+event（自由模式下）");
    Serial.println("  stop             停 record / 清 pending feed");
    Serial.println("  feed <x> <y> <z> 注入一条 accel");
    Serial.println("  help             本文本");
    Serial.println();
}

static void printShow() {
    Serial.println("[D][SHOW] BEGIN");
    Serial.printf("[D][SHOW] face_down  = %+.3f g\n",  g_tuning.face_down_threshold);
    Serial.printf("[D][SHOW] face_up    = %+.3f g\n",  g_tuning.face_up_threshold);
    Serial.printf("[D][SHOW] face_dn_ms = %u\n",        g_tuning.face_down_stable_ms);
    Serial.printf("[D][SHOW] face_up_ms = %u\n",        g_tuning.face_up_stable_ms);
    Serial.printf("[D][SHOW] face_cd    = %u\n",        g_tuning.face_cooldown_ms);
    Serial.printf("[D][SHOW] rotate     = %+.3f g\n",  g_tuning.rotate_threshold);
    Serial.printf("[D][SHOW] rotate_amb = %+.3f g\n",  g_tuning.rotate_amb);
    Serial.printf("[D][SHOW] rotate_ms  = %u\n",        g_tuning.rotate_stable_ms);
    Serial.printf("[D][SHOW] rotate_cd  = %u\n",        g_tuning.rotate_cooldown_ms);
    Serial.printf("[D][SHOW] shake      = %+.3f g\n",  g_tuning.shake_threshold);
    Serial.printf("[D][SHOW] shake_return = %+.3f g\n", g_tuning.shake_return_threshold);
    Serial.printf("[D][SHOW] shake_settle = %+.3f g\n", g_tuning.shake_settle_threshold);
    Serial.printf("[D][SHOW] shake_win  = %u ms\n",      g_tuning.shake_window_ms);
    Serial.printf("[D][SHOW] shake_cd   = %u\n",        g_tuning.shake_cooldown_ms);
    Serial.printf("[D][SHOW] shake_inv  = %u  (%s)\n",  g_tuning.shake_invert,
                   g_tuning.shake_invert ? "inverted" : "default ax>0 → LEFT");
    Serial.printf("[D][SHOW] shake_en   = %u  (%s)\n",  g_tuning.gesture_shake_enabled,
                   g_tuning.gesture_shake_enabled ? "physical shake ON (wizard)"
                                                 : "physical shake OFF (A/B buttons take over)");
    Serial.printf("[D][SHOW] shake_fire = %u  (outbound trigger)\n", g_tuning.shake_fire_on_outbound);
    Serial.printf("[D][SHOW] tap_high   = %+.3f g\n",  g_tuning.tap_z_high);
    Serial.printf("[D][SHOW] tap_low    = %+.3f g\n",  g_tuning.tap_z_low);
    Serial.printf("[D][SHOW] tap_cd     = %u\n",        g_tuning.tap_cooldown_ms);
    Serial.printf("[D][SHOW] verbose    = %u  (1Hz heartbeat %s)\n",
                   g_tuning.verbose, g_tuning.verbose ? "ON" : "OFF");
    Serial.println("[D][SHOW] END");
}

static void printState() {
    Serial.printf("[state] orient=%s  test=%s  step=%u/%u  recording=%d  feed=%d\n",
                  orientName(g_gesture.orientation()),
                  testStateName(g_test_state),
                  g_test_state == TEST_OFF ? 0 : (g_test_step_idx + 1),
                  NUM_STEPS,
                  (int)g_recording,
                  (int)g_pending_feed);
}

// ---------------------------------------------------------------------------
// 命令 handler
// ---------------------------------------------------------------------------

static void cmdSet(char* name, char* val) {
    fieldref::Ref ref;
    if (!findField(name, ref)) {
        Serial.printf("  unknown '%s' (try 'show')\n", name);
        return;
    }
    char* end = nullptr;
    float v = strtof(val, &end);
    if (end == val) {
        Serial.printf("  bad value '%s'\n", val);
        return;
    }
    if (ref.kind == fieldref::F_FLOAT) {
        *ref.p.f = v;
        Serial.printf("  %s = %+.3f\n", name, v);
    } else if (ref.kind == fieldref::F_U16) {
        if (v < 0) v = 0;
        if (v > 60000) v = 60000;
        *ref.p.u = (uint16_t)v;
        Serial.printf("  %s = %u\n", name, (unsigned)*ref.p.u);
    } else {
        *ref.p.b = (v != 0) ? 1 : 0;
        Serial.printf("  %s = %u\n", name, (unsigned)*ref.p.b);
    }
}

static void cmdFeed(char* xs, char* ys, char* zs) {
    float x = strtof(xs, nullptr);
    float y = strtof(ys, nullptr);
    float z = strtof(zs, nullptr);
    g_pending_feed = true;
    g_pending_feed_acc.valid = true;
    g_pending_feed_acc.x = x;
    g_pending_feed_acc.y = y;
    g_pending_feed_acc.z = z;
    g_pending_feed_acc.fetchedAtMs = 0;
    Serial.printf("  queued feed x=%.3f y=%.3f z=%.3f\n", x, y, z);
}

static void cmdReset() {
    // 与 src/gesture.cpp 初始化时一致
    g_tuning.face_down_threshold = defaults::G_FACE_DOWN_THRESHOLD;
    g_tuning.face_up_threshold   = defaults::G_FACE_UP_THRESHOLD;
    g_tuning.face_down_stable_ms = defaults::T_FACE_DOWN_STABLE_MS;
    g_tuning.face_up_stable_ms   = defaults::T_FACE_UP_STABLE_MS;
    g_tuning.face_cooldown_ms    = 2000;
    g_tuning.rotate_threshold    = defaults::G_ROTATE_THRESHOLD;
    g_tuning.rotate_amb          = 0.4f;
    g_tuning.rotate_stable_ms    = defaults::T_ROTATE_STABLE_MS;
    g_tuning.rotate_cooldown_ms  = 1000;
    g_tuning.shake_threshold     = defaults::G_SHAKE_THRESHOLD;
    g_tuning.shake_return_threshold = 0.20f;
    g_tuning.shake_settle_threshold = 0.14f;
    g_tuning.shake_window_ms     = 650;
    g_tuning.shake_cooldown_ms   = defaults::T_SHAKE_COOLDOWN_MS;
    g_tuning.shake_invert        = 0;
    g_tuning.shake_fire_on_outbound = 1;
    g_tuning.gesture_shake_enabled = 0;   // 默认屏蔽物理摇动，A/B 接管
    g_tuning.tap_z_high          = 1.2f;
    g_tuning.tap_z_low           = 1.1f;
    g_tuning.tap_cooldown_ms     = 300;
    g_tuning.verbose             = 0;
    Serial.println("  g_tuning reset to config.h defaults");
}

// ---------------------------------------------------------------------------
// Wizard 状态转换
// ---------------------------------------------------------------------------

static void showStepPrompt() {
    const GestureStep& s = STEPS[g_test_step_idx];
    Serial.println();
    Serial.printf("─── Step %u/%u  %s ───\n", g_test_step_idx + 1, NUM_STEPS, s.name);
    Serial.print  ("  📋 "); Serial.println(s.instruction);
    Serial.print  ("  🎛  可调: ");
    for (uint8_t i = 0; i < s.num_tunables; i++) {
        Serial.print(s.tunables[i]);
        if (i < s.num_tunables - 1) Serial.print(" / ");
    }
    Serial.println();
    Serial.println("  → 按 ENTER 开始录制  （录完后会显示 az/|a| 统计 + 是否触发）");
    Serial.println("  → set <name> <v> 调参 → ENTER 重录");
    Serial.println("  → next / prev / skip / redo / exit");
    printPrompt();
}

static void startCapture() {
    g_test_state         = TEST_CAPTURING;
    g_capture_started_ms = millis();
    g_sum_az = 0; g_min_az =  1e9f; g_max_az = -1e9f;
    g_sum_a  = 0; g_min_a  =  1e9f; g_max_a  = -1e9f;
    g_samples = 0;
    g_fired   = GESTURE_NONE;
    g_fired_at = 0;
    g_have_first_ax = false;
    g_first_ax = 0;
    g_peak_abs_ax = 0;
    // begin() 重置了所有冷却、滑动窗、跨帧计时器 —— 干净起步
    g_gesture.begin();
    Serial.printf("  ⏺ recording up to %u ms, do it now...\n",
                  STEPS[g_test_step_idx].max_ms);
}

static void endCapture(bool force) {
    g_test_state = TEST_FEEDBACK;
    const GestureStep& s = STEPS[g_test_step_idx];
    const uint32_t dur = millis() - g_capture_started_ms;

    Serial.println();
    Serial.printf("─── %s 结果 ───\n", s.name);
    Serial.printf("  samples : %lu  duration: %lu ms", g_samples, dur);
    if (force) Serial.print("  (stopped early)");
    Serial.println();

    if (g_samples > 0) {
        Serial.printf("  az      : min=%+.2f  max=%+.2f  avg=%+.2f\n",
                      g_min_az, g_max_az, g_sum_az / g_samples);
        Serial.printf("  |a|     : min=%.2f   max=%.2f   avg=%.2f\n",
                      g_min_a, g_max_a, g_sum_a / g_samples);
    } else {
        Serial.println("  (no valid samples)");
    }

    if (g_fired != GESTURE_NONE) {
        const uint32_t t = g_fired_at - g_capture_started_ms;
        bool matches = (g_fired == s.expected);
        Serial.printf("  fired   : %s @ +%lu ms  (expect %s)\n",
                      gestureEventName(g_fired), t, gestureEventName(s.expected));
        const bool shake_step = s.expected == GESTURE_SHAKE_LEFT || s.expected == GESTURE_SHAKE_RIGHT;
        if (shake_step && (g_fired == GESTURE_SHAKE_LEFT || g_fired == GESTURE_SHAKE_RIGHT)) {
            Serial.printf("  peak |ax|: %.2f g\n", g_peak_abs_ax);
            if (!matches) {
                g_tuning.shake_invert = g_tuning.shake_invert ? 0 : 1;
                matches = true;
                Serial.printf("  ↺ 已自动修正左右映射：shake_invert=%u\n", g_tuning.shake_invert);
            }
        }
        Serial.println(matches ? "  ✓ OK" : "  ✗ MISMATCH  → 调上面 🎛 字段再试");
    } else {
        Serial.println("  fired   : (none)");
        const bool shake_step = s.expected == GESTURE_SHAKE_LEFT || s.expected == GESTURE_SHAKE_RIGHT;
        if (!force && shake_step) {
            Serial.printf("  peak |ax|: %.2f g\n", g_peak_abs_ax);
            if (g_peak_abs_ax >= 0.20f && g_peak_abs_ax < g_tuning.shake_threshold) {
                g_tuning.shake_threshold = recommendShakeThreshold(g_peak_abs_ax);
                Serial.printf("  ↘ 已自动设置 shake=%.2f；直接按 ENTER 重试\n",
                              g_tuning.shake_threshold);
            } else {
                Serial.println("  峰值已足够：请完整甩出、反向拉回，并停稳约 0.1 秒");
            }
        } else {
            Serial.println("  ✗ 没触发 → 阈值可能太严，或动作不够明显");
        }
    }
    Serial.println();
    Serial.println("  → ENTER 重录  /  next 下一步  /  prev 上一步  /  skip 跳过  /  exit 退出");
    printPrompt();
}

static void goStep(uint8_t idx) {
    g_test_step_idx = idx % NUM_STEPS;
    g_test_state    = TEST_PROMPT;
    // 每切一步都重置手势引擎 —— 让每步的滑动窗 / 冷却 / 跨帧计时器干净起步
    g_gesture.begin();
    showStepPrompt();
}

// ---------------------------------------------------------------------------
// 行解析
// ---------------------------------------------------------------------------
static uint8_t tokenize(char* line, char** argv, uint8_t argv_max) {
    uint8_t argc = 0;
    char* p = line;
    while (*p && argc < argv_max) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return argc;
}

static void processLine(char* line) {
    // 修剪尾部
    size_t n = strlen(line);
    while (n > 0 && (line[n-1] == ' ' || line[n-1] == '\t' || line[n-1] == '\r')) {
        line[--n] = '\0';
    }

    // 空行 → wizard 上下文相关
    if (n == 0) {
        if (g_test_state == TEST_PROMPT || g_test_state == TEST_FEEDBACK) {
            startCapture();
        } else if (g_test_state == TEST_CAPTURING) {
            endCapture(true);
        } else {
            printPrompt();
        }
        return;
    }

    char* argv[8];
    uint8_t argc = tokenize(line, argv, 8);
    if (argc == 0) { printPrompt(); return; }
    const char* cmd = argv[0];

    // 全局命令（任何时候）
    if (!strcmp(cmd, "help") || !strcmp(cmd, "?") || !strcmp(cmd, "h")) {
        printHelp(); printPrompt(); return;
    }
    if (!strcmp(cmd, "exit") || !strcmp(cmd, "quit") || !strcmp(cmd, "q")) {
        if (g_test_state != TEST_OFF) {
            g_test_state = TEST_OFF;
            // 退出 wizard：屏蔽物理摇动，恢复 A/B 操作模式
            g_tuning.gesture_shake_enabled = 0;
            Serial.println("  wizard OFF (physical shake re-blocked)");
        } else {
            Serial.println("  (already out of wizard)");
        }
        printPrompt();
        return;
    }
    if (!strcmp(cmd, "show"))     { printShow();    printPrompt(); return; }
    if (!strcmp(cmd, "state"))    { printState();   printPrompt(); return; }
    if (!strcmp(cmd, "reset"))    { cmdReset();     printPrompt(); return; }
    if (!strcmp(cmd, "set")) {
        if (argc < 3) { Serial.println("  usage: set <name> <value>"); }
        else          { cmdSet(argv[1], argv[2]); }
        printPrompt(); return;
    }
    if (!strcmp(cmd, "verbose")) {
        uint8_t v;
        if (argc >= 2) v = (atoi(argv[1]) ? 1 : 0);
        else           v = g_tuning.verbose ? 0 : 1;
        g_tuning.verbose = v;
        Serial.printf("  verbose = %u  (1Hz heartbeat %s)\n",
                      v, v ? "ON" : "OFF");
        printPrompt(); return;
    }
    if (!strcmp(cmd, "record")) {
        g_recording = true;
        Serial.println("  recording ON  (Ctrl-C / 'stop' to end)");
        printPrompt(); return;
    }
    if (!strcmp(cmd, "stop")) {
        g_recording = false;
        g_pending_feed = false;
        Serial.println("  recording OFF, pending feed cleared");
        printPrompt(); return;
    }
    if (!strcmp(cmd, "feed")) {
        if (argc < 4) Serial.println("  usage: feed <x> <y> <z>");
        else          cmdFeed(argv[1], argv[2], argv[3]);
        printPrompt(); return;
    }

    // Wizard 命令
    if (!strcmp(cmd, "test") || !strcmp(cmd, "t")) {
        // 进入 wizard：临时开物理摇动，方便用户调 shake 阈值
        g_tuning.gesture_shake_enabled = 1;
        goStep(0);  // 含 g_gesture.begin() —— 干净起步
        return;
    }
    if (g_test_state != TEST_OFF) {
        if (!strcmp(cmd, "next") || !strcmp(cmd, "n")) { goStep((g_test_step_idx + 1) % NUM_STEPS);            return; }
        if (!strcmp(cmd, "prev") || !strcmp(cmd, "p")) { goStep((g_test_step_idx + NUM_STEPS - 1) % NUM_STEPS); return; }
        if (!strcmp(cmd, "skip") || !strcmp(cmd, "s")) { goStep((g_test_step_idx + 1) % NUM_STEPS);            return; }
        if (!strcmp(cmd, "redo") || !strcmp(cmd, "r")) { goStep(g_test_step_idx);                              return; }
    }

    Serial.printf("  unknown '%s' (try 'help')\n", cmd);
    printPrompt();
}

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------

void dn_tuning_setup() {
    g_line_len          = 0;
    g_pending_feed      = false;
    g_recording         = false;
    g_record_last_ms    = 0;
    g_test_state        = TEST_OFF;
    g_test_step_idx     = 0;
    g_capture_started_ms = 0;
    g_samples = 0; g_sum_az = 0; g_min_az = 1e9f; g_max_az = -1e9f;
    g_sum_a = 0;  g_min_a  = 1e9f; g_max_a  = -1e9f;
    g_fired = GESTURE_NONE; g_fired_at = 0;
    Serial.println();
    Serial.println("[D][TUNE] gesture REPL ready.  Type 'help' or 'test' to start.");
    printPrompt();
}

bool dn_tuning_take_feed(AccelReading& out) {
    if (!g_pending_feed) return false;
    out = g_pending_feed_acc;
    out.fetchedAtMs = millis();
    g_pending_feed = false;
    return true;
}

void dn_tuning_post_step(uint32_t now_ms, const AccelReading& acc, GestureEvent e) {
    // 1) 吃串口
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n' || g_line_len >= TUNING_LINE_MAX - 1) {
            g_line[g_line_len] = '\0';
            processLine(g_line);
            g_line_len = 0;
        } else {
            g_line[g_line_len++] = c;
        }
    }

    // 2) Recording 流（自由模式）
    if (g_recording && (now_ms - g_record_last_ms) >= RECORD_PERIOD_MS) {
        g_record_last_ms = now_ms;
        Serial.printf("[D][TELEM] t=%lu sample=%lu ax=%+.3f ay=%+.3f az=%+.3f mag=%.3f "
                      "base=%+.3f motion=%+.3f phase=%s dir=%s event=%s orient=%s "
                      "shake=%.3f ret=%.3f settle=%.3f win=%u cd=%u inv=%u\n",
                      (unsigned long)now_ms,
                      (unsigned long)acc.fetchedAtMs,
                      acc.x, acc.y, acc.z,
                      sqrtf(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z),
                      g_gesture.shakeBaselineAx(),
                      g_gesture.shakeMotionAx(acc.x),
                      shakePhaseName(g_gesture.shakePhase()),
                      shakeDirectionName(g_gesture.shakeDirection()),
                      gestureEventName(e),
                      orientName(g_gesture.orientation()),
                      g_tuning.shake_threshold,
                      g_tuning.shake_return_threshold,
                      g_tuning.shake_settle_threshold,
                      g_tuning.shake_window_ms,
                      g_tuning.shake_cooldown_ms,
                      g_tuning.shake_invert);
    }

    // 3) Wizard 录制
    if (g_test_state == TEST_CAPTURING) {
        if (acc.valid) {
            if (!g_have_first_ax) {
                g_first_ax = acc.x;
                g_have_first_ax = true;
            }
            const float abs_ax = fabsf(acc.x);
            if (abs_ax > g_peak_abs_ax) g_peak_abs_ax = abs_ax;
            const float a = sqrtf(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z);
            if (acc.z < g_min_az) g_min_az = acc.z;
            if (acc.z > g_max_az) g_max_az = acc.z;
            g_sum_az += acc.z;
            if (a < g_min_a)  g_min_a  = a;
            if (a > g_max_a)  g_max_a  = a;
            g_sum_a  += a;
            g_samples++;
        }
        if (e != GESTURE_NONE && g_fired == GESTURE_NONE &&
            dn_tuning_event_matches(STEPS[g_test_step_idx].expected, e)) {
            g_fired    = e;
            g_fired_at = now_ms;
            endCapture(false);
            return;
        }
        // 超时
        if (now_ms - g_capture_started_ms >= STEPS[g_test_step_idx].max_ms) {
            endCapture(false);
        }
    }
}

// ---------------------------------------------------------------------------
// 测试 / 脚本化入口
// ---------------------------------------------------------------------------
void dn_tuning_inject_command(const char* line) {
    if (!line) return;
    char buf[TUNING_LINE_MAX];
    size_t n = strlen(line);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, line, n);
    buf[n] = '\0';
    processLine(buf);
}

} // namespace desknest
