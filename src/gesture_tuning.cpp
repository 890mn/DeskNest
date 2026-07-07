// src/gesture_tuning.cpp
// 栖屏 DeskNest - 手势调参串口 REPL 实现
// "栖于桌面，息于常亮之间"

#include "gesture_tuning.h"

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

namespace desknest {

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

// 行缓冲（用户敲到 '\n' 才解析）
// 不用 LINE_MAX —— 会被 xtensa 工具链 <limits.h> 里的同名宏撞掉
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

// 事件名表（state / record 行用）
static const char* gestureEventName(GestureEvent e) {
    switch (e) {
        case GESTURE_NONE:                       return "NONE";
        case GESTURE_SHAKE_LEFT:                 return "SHAKE_L";
        case GESTURE_SHAKE_RIGHT:                return "SHAKE_R";
        case GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE:return "ROT_P2L";
        case GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT:return "ROT_L2P";
        case GESTURE_FACE_DOWN:                  return "FACE_DN";
        case GESTURE_FACE_UP_OPEN:               return "FACE_UP";
        case GESTURE_TAP:                        return "TAP";
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

// ---------------------------------------------------------------------------
// 命令实现
// ---------------------------------------------------------------------------

static void printPrompt() { Serial.print("g> "); }

static void printHelp() {
    Serial.println("Commands:");
    Serial.println("  help                Show this help");
    Serial.println("  state               orient / last event / pending cooldowns");
    Serial.println("  show                All current thresholds (g_tuning)");
    Serial.println("  set <name> <value>  Modify a threshold (see 'show' for names)");
    Serial.println("  reset               Reset g_tuning to defaults from config.h");
    Serial.println("  feed <x> <y> <z>    Inject a single accel reading (g) once");
    Serial.println("  record              Start streaming accel + event @ 20Hz");
    Serial.println("  stop                Stop streaming / clear pending feed");
    Serial.println("");
    Serial.println("Threshold names: face_down, face_up, face_dn_ms, face_up_ms, face_cd,");
    Serial.println("                 rotate, rotate_amb, rotate_ms, rotate_cd,");
    Serial.println("                 shake, shake_cd,");
    Serial.println("                 tap_high, tap_low, tap_cd");
    Serial.println("");
    Serial.println("Examples:");
    Serial.println("  g> set face_down 0.85");
    Serial.println("  g> feed 0.0 1.0 0.0");
    Serial.println("  g> record");
}

static void printShow() {
    Serial.println("[g_tuning] current thresholds:");
    Serial.printf("  face_down  = %+.3f g\n",  g_tuning.face_down_threshold);
    Serial.printf("  face_up    = %+.3f g\n",  g_tuning.face_up_threshold);
    Serial.printf("  face_dn_ms = %u ms\n",     g_tuning.face_down_stable_ms);
    Serial.printf("  face_up_ms = %u ms\n",     g_tuning.face_up_stable_ms);
    Serial.printf("  face_cd    = %u ms\n",     g_tuning.face_cooldown_ms);
    Serial.printf("  rotate     = %+.3f g\n",  g_tuning.rotate_threshold);
    Serial.printf("  rotate_amb = %+.3f g\n",  g_tuning.rotate_amb);
    Serial.printf("  rotate_ms  = %u ms\n",     g_tuning.rotate_stable_ms);
    Serial.printf("  rotate_cd  = %u ms\n",     g_tuning.rotate_cooldown_ms);
    Serial.printf("  shake      = %+.3f g\n",  g_tuning.shake_threshold);
    Serial.printf("  shake_cd   = %u ms\n",     g_tuning.shake_cooldown_ms);
    Serial.printf("  tap_high   = %+.3f g\n",  g_tuning.tap_z_high);
    Serial.printf("  tap_low    = %+.3f g\n",  g_tuning.tap_z_low);
    Serial.printf("  tap_cd     = %u ms\n",     g_tuning.tap_cooldown_ms);
}

static void printState() {
    Serial.printf("[state] orient=%s pending_feed=%d recording=%d\n",
                  orientName(g_gesture.orientation()),
                  (int)g_pending_feed,
                  (int)g_recording);
}

// 简单查名字 → 字段指针。返回 true 表示赋值成功。
namespace fieldref {
    enum Kind { F_FLOAT, F_U16 };
    struct Ref {
        Kind kind;
        union {
            float*    f;
            uint16_t* u;
        } p;
    };
}

static bool findField(const char* name, fieldref::Ref& ref) {
    if (!strcmp(name, "face_down"))   { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.face_down_threshold;   return true; }
    if (!strcmp(name, "face_up"))     { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.face_up_threshold;     return true; }
    if (!strcmp(name, "rotate"))      { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.rotate_threshold;      return true; }
    if (!strcmp(name, "rotate_amb"))  { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.rotate_amb;            return true; }
    if (!strcmp(name, "shake"))       { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.shake_threshold;       return true; }
    if (!strcmp(name, "tap_high"))    { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.tap_z_high;            return true; }
    if (!strcmp(name, "tap_low"))     { ref.kind = fieldref::F_FLOAT; ref.p.f = &g_tuning.tap_z_low;             return true; }
    if (!strcmp(name, "face_dn_ms"))  { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.face_down_stable_ms;   return true; }
    if (!strcmp(name, "face_up_ms"))  { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.face_up_stable_ms;     return true; }
    if (!strcmp(name, "face_cd"))     { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.face_cooldown_ms;      return true; }
    if (!strcmp(name, "rotate_ms"))   { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.rotate_stable_ms;      return true; }
    if (!strcmp(name, "rotate_cd"))   { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.rotate_cooldown_ms;    return true; }
    if (!strcmp(name, "shake_cd"))    { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.shake_cooldown_ms;     return true; }
    if (!strcmp(name, "tap_cd"))      { ref.kind = fieldref::F_U16;   ref.p.u = &g_tuning.tap_cooldown_ms;       return true; }
    return false;
}

static void cmdSet(char* name, char* val) {
    fieldref::Ref ref;
    if (!findField(name, ref)) {
        Serial.printf("  unknown name '%s' (try 'show')\n", name);
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
    } else {
        if (v < 0) v = 0;
        if (v > 60000) v = 60000;
        *ref.p.u = (uint16_t)v;
        Serial.printf("  %s = %u\n", name, (unsigned)*ref.p.u);
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
    g_pending_feed_acc.fetchedAtMs = 0;   // 由 take_feed 填充
    Serial.printf("  queued feed x=%.3f y=%.3f z=%.3f (next loop iteration)\n", x, y, z);
}

static void cmdReset() {
    // 直接重置成 config.h::defaults（保持和 gesture.cpp 初始化时一致的值）
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
    g_tuning.shake_window_ms     = 200;
    g_tuning.shake_cooldown_ms   = defaults::T_SHAKE_COOLDOWN_MS;
    g_tuning.tap_z_high          = 1.2f;
    g_tuning.tap_z_low           = 1.1f;
    g_tuning.tap_cooldown_ms     = 300;
    Serial.println("  g_tuning reset to defaults from config.h");
}

// ---------------------------------------------------------------------------
// 行解析 —— 把一行切成 argv（原地改 '\0'）；返回 argc
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
    // 修剪尾部空白
    size_t n = strlen(line);
    while (n > 0 && (line[n-1] == ' ' || line[n-1] == '\t' || line[n-1] == '\r')) {
        line[--n] = '\0';
    }
    if (n == 0) {
        printPrompt();
        return;
    }

    char* argv[8];
    uint8_t argc = tokenize(line, argv, 8);
    if (argc == 0) {
        printPrompt();
        return;
    }

    const char* cmd = argv[0];

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        printHelp();
    } else if (!strcmp(cmd, "state")) {
        printState();
    } else if (!strcmp(cmd, "show")) {
        printShow();
    } else if (!strcmp(cmd, "reset")) {
        cmdReset();
    } else if (!strcmp(cmd, "record")) {
        g_recording = true;
        Serial.println("  recording ON");
    } else if (!strcmp(cmd, "stop")) {
        g_recording = false;
        g_pending_feed = false;
        Serial.println("  recording OFF, pending feed cleared");
    } else if (!strcmp(cmd, "set")) {
        if (argc < 3) {
            Serial.println("  usage: set <name> <value>");
        } else {
            cmdSet(argv[1], argv[2]);
        }
    } else if (!strcmp(cmd, "feed")) {
        if (argc < 4) {
            Serial.println("  usage: feed <x> <y> <z>");
        } else {
            cmdFeed(argv[1], argv[2], argv[3]);
        }
    } else {
        Serial.printf("  unknown cmd '%s' (try 'help')\n", cmd);
    }
    printPrompt();
}

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------

void dn_tuning_setup() {
    g_line_len = 0;
    g_pending_feed = false;
    g_recording = false;
    g_record_last_ms = 0;
    Serial.println();
    Serial.println("[D][TUNE] gesture REPL ready. Type 'help'.");
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
    // 1) 吃串口输入
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

    // 2) Recording 流
    if (g_recording && (now_ms - g_record_last_ms) >= RECORD_PERIOD_MS) {
        g_record_last_ms = now_ms;
        Serial.printf("[REC t=%lu] x=%+.2f y=%+.2f z=%+.2f |a|=%.2f evt=%s orient=%s\n",
                      (unsigned long)now_ms,
                      acc.x, acc.y, acc.z,
                      sqrtf(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z),
                      gestureEventName(e),
                      orientName(g_gesture.orientation()));
    }
}

// ---------------------------------------------------------------------------
// 测试 / 脚本化入口：等价于在串口敲了一行再按回车。
// 用一个可写副本避免 const 转换问题；返回前清空 line buffer。
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
