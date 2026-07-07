// test/test_gesture/test_gesture_recognition.cpp
// 栖屏 DeskNest — 三轴手势识别 smoke test
// "栖于桌面，息于常亮之间"
//
// 用 PlatformIO native + Unity 在 host 上编译运行 gesture.cpp，
// 校准 plan §4.2 列出的全部阈值 / 滞回 / 冷却时间：
//
//   旋转：|ax|>0.7g ∧ |ay|<0.3g → LANDSCAPE；稳定 400ms 才 commit；冷却 1000ms
//   翻面：az > +0.7g 持续 800ms → FACE_DOWN；冷却 2000ms
//   翻回：az < -0.7g 持续 300ms → FACE_UP_OPEN；冷却 2000ms
//   摇动：|a| 峰值 > 1.5g（200ms 窗口）→ SHAKE；冷却 600ms
//   Tap ：gz spike > 1.2g（prev_gz<1.1）→ TAP；冷却 300ms
//   滞回：中间地带（任一轴都不破阈值）保持当前 orientation
//
// 不烧入 K10；CI / 离线回归用。

#include <unity.h>

// 把 Arduino.h 桩和 gesture.cpp 拉进来。注意：必须放在 Unity <unity.h> 之后，
// 否则 gesture.cpp 的 #include <Arduino.h> 会先吃掉 framework-arduino* 的版本。
#include "../../src/gesture.cpp"
#include "../../src/gesture_tuning.cpp"

using namespace desknest;

// sensors.h 里 extern 了 g_sensors；gesture_tuning.cpp 里的 recording 流会
// 引用它（虽然测试不会进入 recording 路径），链接器需要一份定义。
// 默认构造足够，方法不会被调用。
Sensors g_sensors;

// ---------------------------------------------------------------------------
// 时钟 / 工具
// ---------------------------------------------------------------------------

uint32_t          g_mock_millis = 0;
SerialClass       Serial;

static inline void test_clock_reset()                   { g_mock_millis = 0; }
static inline void test_clock_advance(uint32_t ms)      { g_mock_millis += ms; }

static inline AccelReading accel(float x, float y, float z) {
    AccelReading a = { true, x, y, z, 0 };
    return a;
}

// 喂一组稳定样本直到 sliding window 收齐（≈ 16 个样本 @ 30Hz）
static void feedSettle(GestureEngine& g, float x, float y, float z,
                       uint32_t step_ms = 33) {
    for (int i = 0; i < 16; ++i) {
        g.update(accel(x, y, z), g_mock_millis);
        g_mock_millis += step_ms;
    }
}

void setUp(void)    { test_clock_reset(); }
void tearDown(void) {}

// ===========================================================================
// 1) SlidingWindow：样本数 < N 时按 filled 数取平均；填满后滑动
// ===========================================================================
void test_sliding_window_average_grows_then_slides() {
    SlidingWindow<float, 4> w;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w.avg());

    w.push(1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, w.avg());
    w.push(3.0f);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, w.avg());
    w.push(5.0f);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, w.avg());
    w.push(7.0f);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, w.avg());

    // 覆盖最早样本：期望 (3+5+7+9)/4 = 6
    w.push(9.0f);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, w.avg());

    w.reset();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w.avg());
}

// ===========================================================================
// 2) Hysteresis：candidate 必须维持 stable_ms 才 commit
// ===========================================================================
void test_hysteresis_needs_stable_window_to_commit() {
    Hysteresis<int> h(0);
    h.update(1, 1000, 400);
    TEST_ASSERT_FALSE(h.changed());             // 初始计时，不够 400ms

    h.update(1, 1100, 400);                    // +100ms 累计 100ms < 400
    TEST_ASSERT_FALSE(h.changed());

    h.update(1, 1399, 400);                    // 累计 399ms 仍差 1ms
    TEST_ASSERT_FALSE(h.changed());

    h.update(1, 1400, 400);                    // 累计 400ms 整 → commit
    TEST_ASSERT_TRUE(h.changed());
    TEST_ASSERT_EQUAL(1, h.value());

    // commit 后再次 update：稳定在 v=1，changed 立即回到 false
    h.update(1, 1500, 400);
    TEST_ASSERT_FALSE(h.changed());

    // 抖动会被复位计时器
    Hysteresis<int> h2(0);
    h2.update(1, 1000, 400);
    h2.update(2, 1100, 400);                   // 切到 2，计时器归零
    h2.update(1, 1200, 400);                   // 切回 1，计时器又归零
    TEST_ASSERT_EQUAL(0, h2.value());           // 仍然没 commit
    h2.update(1, 1600, 400);                   // 累计 400ms 后 commit
    TEST_ASSERT_TRUE(h2.changed());
}

// ===========================================================================
// 3) GestureEngine 初始：orientation = PORTRAIT
// ===========================================================================
void test_gesture_initial_orientation_is_portrait() {
    GestureEngine g;
    g.begin();
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, g.orientation());
}

// ===========================================================================
// 4) 旋转 P → L：ax>>0.7, ay<<0.3 稳定 400ms（且超过 1000ms 冷却）才触发
// ===========================================================================
void test_gesture_rotate_portrait_to_landscape() {
    GestureEngine g;
    g.begin();
    // 先把"已运行时间"推到 >1000ms，否则旋转冷却（_last_rotate_ms=0）放行不了
    feedSettle(g, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, g.orientation());

    bool got = false;
    for (int i = 0; i < 20; ++i) {              // 20*33 = 660ms > 400ms stable
        GestureEvent e = g.update(accel(0.9f, 0.1f, 0.0f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE) {
            got = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "ROTATE_P2L not fired within 660ms of landscape");
    TEST_ASSERT_EQUAL(ORIENTATION_LANDSCAPE, g.orientation());
}

// ===========================================================================
// 5) 旋转 L → P：先转横，再翻回竖
// ===========================================================================
void test_gesture_rotate_landscape_to_portrait() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);            // 推过 1000ms 冷却

    // P → L
    bool toLand = false;
    for (int i = 0; i < 20; ++i) {
        if (g.update(accel(0.9f, 0.1f, 0.0f), g_mock_millis)
            == GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE) { toLand = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE(toLand);
    TEST_ASSERT_EQUAL(ORIENTATION_LANDSCAPE, g.orientation());

    // 推过下一个 1000ms 旋转冷却
    feedSettle(g, 0.9f, 0.1f, 0.0f);

    // L → P
    bool toPort = false;
    for (int i = 0; i < 20; ++i) {
        if (g.update(accel(0.0f, 1.0f, 0.0f), g_mock_millis)
            == GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT) { toPort = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE_MESSAGE(toPort, "ROTATE_L2P not fired");
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, g.orientation());
}

// ===========================================================================
// 6) 摇动：200ms 窗口内 |a| > 1.5g 触发 SHAKE
// ===========================================================================
void test_gesture_shake_detected() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);            // 静止，|a|=1.0

    bool got = false;
    for (int i = 0; i < 10; ++i) {              // 10*33 = 330ms
        GestureEvent e = g.update(accel(1.8f, 0.0f, 0.0f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_SHAKE_LEFT || e == GESTURE_SHAKE_RIGHT) {
            got = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "shake not detected with |a|=1.8g");
}

// ===========================================================================
// 7) 摇动冷却：600ms 内第二次摇动被吞掉
// ===========================================================================
void test_gesture_shake_cooldown_blocks_second() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);

    // 第一次摇动
    bool first = false;
    for (int i = 0; i < 10; ++i) {
        if (g.update(accel(1.8f, 0.0f, 0.0f), g_mock_millis)
            == GESTURE_SHAKE_LEFT) { first = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE(first);

    // 紧接着第二次摇动：在 600ms 冷却窗内
    bool secondFired = false;
    for (int i = 0; i < 10; ++i) {              // 10*33 = 330ms < 600ms
        GestureEvent e = g.update(accel(1.8f, 0.0f, 0.0f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_SHAKE_LEFT || e == GESTURE_SHAKE_RIGHT) {
            secondFired = true; break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(secondFired, "second shake fired within 600ms cooldown");

    // 过了冷却后再摇：应当能再触发
    feedSettle(g, 0.0f, 1.0f, 0.0f);            // 静止推过 600ms
    bool third = false;
    for (int i = 0; i < 10; ++i) {
        if (g.update(accel(1.8f, 0.0f, 0.0f), g_mock_millis)
            == GESTURE_SHAKE_LEFT) { third = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE_MESSAGE(third, "shake did not re-arm after cooldown");
}

// ===========================================================================
// 8) 翻面栖息：az > 0.7 持续 800ms → FACE_DOWN（同时跨过 2000ms 冷却）
// ===========================================================================
void test_gesture_face_down_roost() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);            // 推过 2000ms 冷却

    bool got = false;
    for (int i = 0; i < 60; ++i) {              // 60*33 = 1980ms > 800ms stable
        GestureEvent e = g.update(accel(0.0f, 0.0f, 0.95f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_FACE_DOWN) { got = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "FACE_DOWN not fired with az=0.95g for >800ms");
}

// ===========================================================================
// 9) 翻回打开：az < -0.7 持续 300ms → FACE_UP_OPEN
// ===========================================================================
void test_gesture_face_up_open() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);            // 推过冷却

    // 先翻面（拿到 2000ms 冷却起点）
    bool down = false;
    for (int i = 0; i < 60; ++i) {
        if (g.update(accel(0.0f, 0.0f, 0.95f), g_mock_millis)
            == GESTURE_FACE_DOWN) { down = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE(down);

    // 翻回。
    // 留 100 个迭代：滑动窗口从 0.95 过渡到 -0.95 吃掉 ~8 个，
    // 之后要等 2000ms 翻面冷却 + 300ms 稳定 = ~70 个 @ 33ms。
    bool up = false;
    for (int i = 0; i < 100; ++i) {
        GestureEvent e = g.update(accel(0.0f, 0.0f, -0.95f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_FACE_UP_OPEN) { up = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(up, "FACE_UP_OPEN not fired with az=-0.95g for >300ms");
}

// ===========================================================================
// 10) 滞回死区：模糊地带的抖动不翻转姿态
// ===========================================================================
void test_gesture_orientation_holds_in_dead_zone() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);

    // ax=0.4 (低于 0.7 阈值)，ay=0.5 (低于 0.7)
    // classifyOrientation_ 不命中任一分支，返回 _orient.value()=PORTRAIT
    bool rotated = false;
    for (int i = 0; i < 30; ++i) {              // 30*33 = 990ms
        GestureEvent e = g.update(accel(0.4f, 0.5f, 0.5f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE ||
            e == GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT) {
            rotated = true; break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(rotated, "orientation flipped in dead zone (should hold)");
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, g.orientation());
}

// ===========================================================================
// 11) Tap：gz spike > 1.2g 触发（prev_gz 必须先 < 1.1）
// ===========================================================================
void test_gesture_tap_on_z_spike() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);            // z=0 → prev_gz 落到 0（< 1.1）

    bool got = false;
    for (int i = 0; i < 5; ++i) {
        GestureEvent e = g.update(accel(0.0f, 1.0f, 1.3f), g_mock_millis);
        g_mock_millis += 33;
        if (e == GESTURE_TAP) { got = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(got, "TAP not detected on gz spike 0→1.3");
}

// ===========================================================================
// 12) 无效读数：acc.valid=false → GESTURE_NONE
// ===========================================================================
void test_gesture_invalid_sample_returns_none() {
    GestureEngine g;
    g.begin();
    AccelReading bad = { false, 0.0f, 0.0f, 0.0f, 0 };
    TEST_ASSERT_EQUAL(GESTURE_NONE, g.update(bad, g_mock_millis));
}

// ===========================================================================
// 13) g_tuning 运行时改阈值后立即生效
//     把 face_down 阈值从 0.7 放宽到 0.5：az=0.55（默认下不到）
//     现在应该能触发 FACE_DOWN。
// ===========================================================================
void test_tuning_face_down_threshold_runtime_change() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);

    // 默认下：az=0.55 触发不了
    g_tuning.face_down_threshold = 0.6f;
    bool gotDefault = false;
    for (int i = 0; i < 60; ++i) {
        if (g.update(accel(0.0f, 0.0f, 0.55f), g_mock_millis)
            == GESTURE_FACE_DOWN) { gotDefault = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_FALSE_MESSAGE(gotDefault, "FACE_DOWN fired with relaxed threshold — expected miss");

    // 放宽到 0.5：现在 az=0.55 应该触发
    g_tuning.face_down_threshold = 0.5f;
    bool gotRelaxed = false;
    for (int i = 0; i < 60; ++i) {
        if (g.update(accel(0.0f, 0.0f, 0.55f), g_mock_millis)
            == GESTURE_FACE_DOWN) { gotRelaxed = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE_MESSAGE(gotRelaxed, "FACE_DOWN not fired after lowering threshold to 0.5");

    // 恢复默认，避免污染其他测试
    g_tuning.face_down_threshold = defaults::G_FACE_DOWN_THRESHOLD;
}

// ===========================================================================
// 14) g_tuning.rotate_stable_ms 改大后旋转要等更久才 commit
// ===========================================================================
void test_tuning_rotate_stable_ms_runtime_change() {
    GestureEngine g;
    g.begin();
    feedSettle(g, 0.0f, 1.0f, 0.0f);

    // 放宽到 1500ms：400ms 之前肯定不 commit
    g_tuning.rotate_stable_ms = 1500;
    bool rotatedEarly = false;
    for (int i = 0; i < 12; ++i) {  // 12*33 = 396ms < 400ms < 1500ms
        if (g.update(accel(0.9f, 0.1f, 0.0f), g_mock_millis)
            == GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE) { rotatedEarly = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_FALSE_MESSAGE(rotatedEarly, "ROTATE fired before rotate_stable_ms=1500 elapsed");

    // 再走 1500ms：等 commit
    bool rotatedLate = false;
    for (int i = 0; i < 60; ++i) {
        if (g.update(accel(0.9f, 0.1f, 0.0f), g_mock_millis)
            == GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE) { rotatedLate = true; break; }
        g_mock_millis += 33;
    }
    TEST_ASSERT_TRUE_MESSAGE(rotatedLate, "ROTATE never fired even with rotate_stable_ms=1500");

    g_tuning.rotate_stable_ms = defaults::T_ROTATE_STABLE_MS;
}

// ===========================================================================
// 15) REPL: 'set face_down 0.85' 直接改 g_tuning.face_down_threshold
// ===========================================================================
void test_tuning_repl_set_modifies_field() {
    g_tuning.face_down_threshold = 0.7f;  // 先确认一个已知起点
    dn_tuning_inject_command("set face_down 0.85");
    TEST_ASSERT_EQUAL_FLOAT(0.85f, g_tuning.face_down_threshold);

    dn_tuning_inject_command("set face_cd 500");
    TEST_ASSERT_EQUAL_UINT16(500, g_tuning.face_cooldown_ms);

    // 名字错：不报错也不改值
    const float before = g_tuning.face_down_threshold;
    dn_tuning_inject_command("set bogus_name 1.23");
    TEST_ASSERT_EQUAL_FLOAT(before, g_tuning.face_down_threshold);
}

// ===========================================================================
// 16) REPL: 'feed x y z' 排队一次 accel；dn_tuning_take_feed 消费一次后
//     第二次返回 false
// ===========================================================================
void test_tuning_repl_feed_queue_take() {
    // 清掉之前测试可能残留的 pending
    AccelReading dummy;
    while (dn_tuning_take_feed(dummy)) { /* drain */ }

    dn_tuning_inject_command("feed 0.0 0.9 0.1");
    AccelReading got = { false, 0, 0, 0, 0 };
    TEST_ASSERT_TRUE_MESSAGE(dn_tuning_take_feed(got), "take_feed returned false after 'feed'");
    TEST_ASSERT_TRUE(got.valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, got.x);
    TEST_ASSERT_EQUAL_FLOAT(0.9f, got.y);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, got.z);

    // 第二次取：应当空了
    AccelReading got2 = { false, 0, 0, 0, 0 };
    TEST_ASSERT_FALSE_MESSAGE(dn_tuning_take_feed(got2), "take_feed should be one-shot");
}

// ===========================================================================
// 17) REPL: 'reset' 把 g_tuning 恢复成 defaults
// ===========================================================================
void test_tuning_repl_reset_restores_defaults() {
    g_tuning.face_down_threshold = 0.42f;
    g_tuning.rotate_stable_ms    = 9999;
    g_tuning.shake_threshold     = 0.10f;

    dn_tuning_inject_command("reset");

    TEST_ASSERT_EQUAL_FLOAT(defaults::G_FACE_DOWN_THRESHOLD, g_tuning.face_down_threshold);
    TEST_ASSERT_EQUAL_UINT16(defaults::T_ROTATE_STABLE_MS, g_tuning.rotate_stable_ms);
    TEST_ASSERT_EQUAL_FLOAT(defaults::G_SHAKE_THRESHOLD, g_tuning.shake_threshold);
}

// ===========================================================================
// 18) REPL: 'record' / 'stop' 切换 g_recording 状态
//     （g_recording 是文件内 static，无法直接读到；用间接法：
//      'stop' 还会清掉 pending feed，所以拿 feed + take 验证 stop 的副作用）
// ===========================================================================
void test_tuning_repl_record_stop_clears_feed() {
    // 排一个 feed
    AccelReading drain;
    while (dn_tuning_take_feed(drain)) {}
    dn_tuning_inject_command("feed 0.1 0.2 0.3");
    AccelReading pre = { false, 0, 0, 0, 0 };
    TEST_ASSERT_TRUE(dn_tuning_take_feed(pre));   // 消费掉

    // 'stop' 应当清掉 pending feed（即使没有）
    dn_tuning_inject_command("feed 0.4 0.5 0.6");
    dn_tuning_inject_command("stop");
    AccelReading post = { false, 0, 0, 0, 0 };
    TEST_ASSERT_FALSE_MESSAGE(dn_tuning_take_feed(post), "'stop' should clear pending feed");
}

// ===========================================================================
// 入口：Unity main()
//
// 提供自己的 main()：PIO 的 native + unity 不会自动注入 main，
// 链接器用 crtexewin 的默认 main → WinMain 死循环。
// 用 UNITY_BEGIN / RUN_TEST / UNITY_END 显式跑用例。
// ===========================================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_sliding_window_average_grows_then_slides);
    RUN_TEST(test_hysteresis_needs_stable_window_to_commit);
    RUN_TEST(test_gesture_initial_orientation_is_portrait);
    RUN_TEST(test_gesture_rotate_portrait_to_landscape);
    RUN_TEST(test_gesture_rotate_landscape_to_portrait);
    RUN_TEST(test_gesture_shake_detected);
    RUN_TEST(test_gesture_shake_cooldown_blocks_second);
    RUN_TEST(test_gesture_face_down_roost);
    RUN_TEST(test_gesture_face_up_open);
    RUN_TEST(test_gesture_orientation_holds_in_dead_zone);
    RUN_TEST(test_gesture_tap_on_z_spike);
    RUN_TEST(test_gesture_invalid_sample_returns_none);
    RUN_TEST(test_tuning_face_down_threshold_runtime_change);
    RUN_TEST(test_tuning_rotate_stable_ms_runtime_change);
    RUN_TEST(test_tuning_repl_set_modifies_field);
    RUN_TEST(test_tuning_repl_feed_queue_take);
    RUN_TEST(test_tuning_repl_reset_restores_defaults);
    RUN_TEST(test_tuning_repl_record_stop_clears_feed);
    return UNITY_END();
}