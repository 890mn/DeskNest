// DeskNest - non-blocking component test hardware adapter

#include "component_test_module.h"

#include "config.h"

#include <Arduino.h>
#include <IOBOX_Motor.h>
#include "esp32-hal-ledc.h"

#include <stdio.h>
#include <string.h>

namespace desknest {
namespace {

constexpr uint8_t kServoChannel = 6;
constexpr uint16_t kServoPeriodUs = 20000;
constexpr uint8_t kServoResolutionBits = 14;
constexpr uint32_t kServoDutyMax = (1UL << kServoResolutionBits) - 1UL;
constexpr uint8_t kServoForwardDegrees = 180;
constexpr uint8_t kServoReverseDegrees = 0;

constexpr uint8_t kMotorCw = 0x00;
constexpr uint8_t kMotorCcw = 0x01;
constexpr uint8_t kMotorSpeed = 200;
// Keep servo and motor timing independent. The motor follows the direct
// CW -> CCW sequence from the Mind+ MBT0014 example.
constexpr uint32_t kServoMotionRunMs = 1000;
constexpr uint32_t kServoMotionPauseMs = 500;
constexpr uint32_t kMotorMotionRunMs = 1000;

constexpr uint16_t kIrRawMax = 128;

struct IrKeyName {
    uint32_t wiki_code;
    uint16_t legacy_code;
    const char* name;
};

// DFR0094's two published remote tables. The K10 table prints
// address + command + inverted-command (for example, key 1 is FD08F7),
// while the older Arduino table exposes the command/inverted-command pair
// (for example, key 1 is EF10). The edge decoder keeps the complete 32-bit
// NEC frame and matches both representations.
constexpr IrKeyName kIrKeys[] = {
    {0xFD00FFUL, 0xFF00, "红色按钮"},
    {0xFD807FUL, 0xFE01, "VOL+"},
    {0xFD40BFUL, 0xFD02, "FUNC/STOP"},
    {0xFD20DFUL, 0xFB04, "左2个三角"},
    {0xFDA05FUL, 0xFA05, "暂停键"},
    {0xFD609FUL, 0xF906, "右2个三角"},
    {0xFD10EFUL, 0xF708, "向下三角"},
    {0xFD906FUL, 0xF609, "VOL-"},
    {0xFD50AFUL, 0xF50A, "向上三角"},
    {0xFD30CFUL, 0xF30C, "0"},
    {0xFDB04FUL, 0xF20D, "EQ"},
    {0xFD708FUL, 0xF10E, "ST/REPT"},
    {0xFD08F7UL, 0xEF10, "1"},
    {0xFD8877UL, 0xEE11, "2"},
    {0xFD48B7UL, 0xED12, "3"},
    {0xFD28D7UL, 0xEB14, "4"},
    {0xFDA857UL, 0xEA15, "5"},
    {0xFD6897UL, 0xE916, "6"},
    {0xFD18E7UL, 0xE718, "7"},
    {0xFD9867UL, 0xE619, "8"},
    {0xFD58A7UL, 0xE51A, "9"},
};

enum RunKind : uint8_t {
    RUN_NONE = 0,
    RUN_SERVO,
    RUN_MOTOR,
    RUN_IR,
};

ComponentTestRow s_rows[COMPONENT_TEST_MAX_ROWS];
uint8_t s_selected_index = 0;
RunKind s_run = RUN_NONE;
uint8_t s_stage = 0;
uint32_t s_started_ms = 0;
uint32_t s_last_action_ms = 0;
bool s_servo_ready = false;
bool s_motor_failed = false;
IOBOX_Motor s_motor_ib;

volatile bool s_ir_capture = false;
volatile uint32_t s_ir_last_edge_us = 0;
volatile uint16_t s_ir_count = 0;
volatile uint16_t s_ir_widths[kIrRawMax] = {};
volatile uint8_t s_ir_levels[kIrRawMax] = {};

void set_row(uint8_t index,
             const char* name,
             const char* execution,
             const char* content,
             const char* result) {
    if (index >= COMPONENT_TEST_MAX_ROWS) return;
    snprintf(s_rows[index].name, sizeof(s_rows[index].name), "%s", name ? name : "");
    snprintf(s_rows[index].execution, sizeof(s_rows[index].execution),
             "%s", execution ? execution : "");
    snprintf(s_rows[index].content, sizeof(s_rows[index].content),
             "%s", content ? content : "");
    snprintf(s_rows[index].result, sizeof(s_rows[index].result),
             "%s", result ? result : "");
}

void mark_running(uint8_t index) {
    if (index >= COMPONENT_TEST_MAX_ROWS) return;
    snprintf(s_rows[index].execution, sizeof(s_rows[index].execution), "执行中");
    snprintf(s_rows[index].result, sizeof(s_rows[index].result), "测试中");
}

void finish_run(const char* result) {
    if (s_run == RUN_IR) {
        detachInterrupt(digitalPinToInterrupt(DESKNEST_COMPONENT_IR_PIN));
        noInterrupts();
        s_ir_capture = false;
        interrupts();
    }
    s_run = RUN_NONE;
    snprintf(s_rows[s_selected_index].execution,
             sizeof(s_rows[s_selected_index].execution), "已执行");
    snprintf(s_rows[s_selected_index].result,
             sizeof(s_rows[s_selected_index].result), "%s", result ? result : "");
}

void servo_write_degrees(uint8_t degrees) {
    if (!s_servo_ready) return;
    if (degrees > 180) degrees = 180;
    const uint32_t pulse_us = 500UL + (2000UL * degrees) / 180UL;
    const uint32_t duty = (pulse_us * kServoDutyMax) / kServoPeriodUs;
    ledcWrite(kServoChannel, duty);
}

bool motor_run(uint8_t direction) {
    const uint8_t library_direction =
        direction == kMotorCcw ? s_motor_ib.CCW : s_motor_ib.CW;
    s_motor_ib.motorRun(s_motor_ib.M1, library_direction, kMotorSpeed);
    const uint8_t error = s_motor_ib.lastI2cError();
    const bool ok = error == 0;
    if (!ok) {
        Serial.printf("[D][COMPONENT] motor I2C error=%u\n", (unsigned)error);
    }
    Serial.printf("[D][COMPONENT] motor M1 %s direction=%u speed=%u ok=%u\n",
                  direction == kMotorCcw ? "CCW" : "CW",
                  (unsigned)direction,
                  (unsigned)kMotorSpeed,
                  (unsigned)ok);
    return ok;
}

bool motor_stop() {
    s_motor_ib.motorStop(s_motor_ib.M1);
    const uint8_t error = s_motor_ib.lastI2cError();
    const bool ok = error == 0;
    if (!ok) {
        Serial.printf("[D][COMPONENT] motor I2C error=%u\n", (unsigned)error);
    }
    Serial.printf("[D][COMPONENT] motor M1 STOP ok=%u\n", (unsigned)ok);
    return ok;
}

void IRAM_ATTR component_ir_edge_isr() {
    const uint32_t now_us = micros();
    const uint32_t width_us = now_us - s_ir_last_edge_us;
    s_ir_last_edge_us = now_us;
    if (!s_ir_capture || width_us > 30000UL) return;

    const uint16_t count = s_ir_count;
    if (count >= kIrRawMax) return;

    // The interrupt fires on the new level. Store the level that just ended,
    // which makes the NEC decoder independent of the first idle-high gap.
    const bool current_high = digitalRead(DESKNEST_COMPONENT_IR_PIN) != 0;
    s_ir_widths[count] = (uint16_t)width_us;
    s_ir_levels[count] = current_high ? 1 : 0; // previous LOW=1, HIGH=0
    s_ir_count = count + 1;
}

void start_ir_capture() {
    pinMode(DESKNEST_COMPONENT_IR_PIN, INPUT_PULLUP);
    detachInterrupt(digitalPinToInterrupt(DESKNEST_COMPONENT_IR_PIN));
    noInterrupts();
    s_ir_count = 0;
    s_ir_last_edge_us = micros();
    s_ir_capture = true;
    interrupts();
    attachInterrupt(digitalPinToInterrupt(DESKNEST_COMPONENT_IR_PIN),
                    component_ir_edge_isr, CHANGE);
}

void reset_ir_capture_buffer() {
    noInterrupts();
    s_ir_count = 0;
    s_ir_last_edge_us = micros();
    interrupts();
}

void copy_ir_capture(uint16_t* widths, uint8_t* levels, uint8_t* count) {
    if (!widths || !levels || !count) return;
    noInterrupts();
    uint16_t n = s_ir_count;
    if (n > kIrRawMax) n = kIrRawMax;
    for (uint16_t i = 0; i < n; ++i) {
        widths[i] = s_ir_widths[i];
        levels[i] = s_ir_levels[i];
    }
    interrupts();
    *count = (uint8_t)n;
}

bool decode_nec(const uint16_t* widths,
                const uint8_t* levels,
                uint8_t count,
                uint32_t* code_out) {
    if (!widths || !levels || !code_out || count < 66) return false;

    for (uint16_t lead = 0; lead + 1 < count; ++lead) {
        if (levels[lead] != 1 || levels[lead + 1] != 0) continue;
        if (widths[lead] < 8000 || widths[lead] > 10000) continue;
        if (widths[lead + 1] < 3500 || widths[lead + 1] > 5500) continue;

        const uint16_t first_bit = lead + 2;
        if (first_bit + 63 >= count) continue;

        uint32_t code = 0;
        bool valid = true;
        for (uint8_t bit = 0; bit < 32; ++bit) {
            const uint16_t low = first_bit + (uint16_t)bit * 2;
            const uint16_t high = low + 1;
            if (levels[low] != 1 || levels[high] != 0) {
                valid = false;
                break;
            }
            if (widths[low] < 300 || widths[low] > 800) {
                valid = false;
                break;
            }
            if (widths[high] >= 1100 && widths[high] <= 2200) {
                code |= (1UL << bit);
            } else if (widths[high] < 300 || widths[high] > 900) {
                valid = false;
                break;
            }
        }
        if (valid) {
            *code_out = code;
            return true;
        }
    }
    return false;
}

const char* ir_key_name(uint32_t code) {
    const uint32_t wiki_code =
        ((code & 0xFFUL) << 16) |
        (((code >> 16) & 0xFFUL) << 8) |
        ((code >> 24) & 0xFFUL);
    const uint16_t legacy_code = (uint16_t)((code >> 16) & 0xFFFFUL);
    for (const IrKeyName& key : kIrKeys) {
        if (key.wiki_code == wiki_code || key.legacy_code == legacy_code) {
            return key.name;
        }
    }
    return nullptr;
}

void reset_rows() {
    memset(s_rows, 0, sizeof(s_rows));
    set_row(0, "普通舵机", "未执行", "P0: 正转→暂停→反转→暂停", "—");
    set_row(1, "直流电机", "未执行", "M1: 正转→反转→暂停", "—");
    set_row(2, "红外遥控", "未执行", "P1: 等待按键", "—");
}

} // namespace

void dn_component_test_begin() {
    s_selected_index = 0;
    s_run = RUN_NONE;
    s_stage = 0;
    s_motor_failed = false;
    reset_rows();

    const uint32_t configured = ledcSetup(kServoChannel, 50, kServoResolutionBits);
    s_servo_ready = configured != 0;
    if (s_servo_ready) {
        ledcAttachPin(DESKNEST_COMPONENT_SERVO_PIN, kServoChannel);
        servo_write_degrees(90);
    } else {
        Serial.printf("[E][COMPONENT] servo LEDC setup failed channel=%u resolution=%u\n",
                      (unsigned)kServoChannel, (unsigned)kServoResolutionBits);
    }
    pinMode(DESKNEST_COMPONENT_IR_PIN, INPUT_PULLUP);
    Serial.printf("[D][COMPONENT] ready servo=P%u ir=P%u motor=0x%02X\n",
                  (unsigned)DESKNEST_COMPONENT_SERVO_PIN,
                  (unsigned)DESKNEST_COMPONENT_IR_PIN,
                  (unsigned)DESKNEST_COMPONENT_MOTOR_I2C_ADDR);
}

void dn_component_test_select_next() {
    if (s_run != RUN_NONE) {
        dn_component_test_abort();
    }
    s_selected_index = (uint8_t)((s_selected_index + 1) % COMPONENT_TEST_MAX_ROWS);
    Serial.printf("[D][COMPONENT] select=%u\n", (unsigned)s_selected_index);
}

void dn_component_test_execute() {
    if (s_run != RUN_NONE || s_selected_index >= COMPONENT_TEST_MAX_ROWS) return;

    mark_running(s_selected_index);
    s_started_ms = millis();
    s_last_action_ms = s_started_ms;
    s_stage = 0;
    s_motor_failed = false;

    switch (s_selected_index) {
        case 0:
            s_run = RUN_SERVO;
            if (!s_servo_ready) {
                finish_run("FAIL");
                return;
            }
            servo_write_degrees(kServoForwardDegrees);
            Serial.println("[D][COMPONENT] servo sequence start");
            break;
        case 1:
            s_run = RUN_MOTOR;
            s_motor_failed = !motor_run(kMotorCw);
            Serial.println("[D][COMPONENT] motor sequence start");
            break;
        case 2:
            s_run = RUN_IR;
            start_ir_capture();
            snprintf(s_rows[2].content, sizeof(s_rows[2].content),
                     "P1: 等待按键");
            snprintf(s_rows[2].execution, sizeof(s_rows[2].execution), "检测中");
            snprintf(s_rows[2].result, sizeof(s_rows[2].result), "监听中");
            Serial.println("[D][COMPONENT] IR NEC key capture start");
            break;
        default:
            break;
    }
}

void dn_component_test_abort() {
    if (s_run == RUN_NONE) return;
    if (s_run == RUN_SERVO && s_servo_ready) {
        ledcWrite(kServoChannel, 0);
    }
    if (s_run == RUN_MOTOR && !motor_stop()) s_motor_failed = true;
    if (s_run == RUN_IR) {
        detachInterrupt(digitalPinToInterrupt(DESKNEST_COMPONENT_IR_PIN));
        noInterrupts();
        s_ir_capture = false;
        interrupts();
    }
    s_run = RUN_NONE;
    snprintf(s_rows[s_selected_index].execution,
             sizeof(s_rows[s_selected_index].execution), "已执行");
    snprintf(s_rows[s_selected_index].result,
             sizeof(s_rows[s_selected_index].result), "中止");
}

void dn_component_test_tick(uint32_t now_ms) {
    if (s_run == RUN_NONE) return;

    // Protect the non-blocking timers from a caller timestamp captured just
    // before execute() stamped s_last_action_ms. The signed subtraction also
    // remains valid across the normal uint32_t millis() wrap window.
    const int32_t elapsed_since_action =
        (int32_t)(now_ms - s_last_action_ms);
    if (elapsed_since_action < 0) return;

    if (s_run == RUN_SERVO) {
        const uint32_t wait_ms = (s_stage == 0 || s_stage == 2)
            ? kServoMotionRunMs : kServoMotionPauseMs;
        if ((uint32_t)elapsed_since_action < wait_ms) return;
        if (s_stage == 0) {
            // Forward reached its endpoint; hold there for a pause.
            s_stage = 1;
        } else if (s_stage == 1) {
            servo_write_degrees(kServoReverseDegrees);
            s_stage = 2;
        } else if (s_stage == 2) {
            // Reverse reached its endpoint; the servo test is complete.
            finish_run("PASS");
            return;
        } else {
            finish_run("PASS");
            return;
        }
        s_last_action_ms = now_ms;
        return;
    }

    if (s_run == RUN_MOTOR) {
        const uint32_t wait_ms = kMotorMotionRunMs;
        if ((uint32_t)elapsed_since_action < wait_ms) return;
        if (s_stage == 0) {
            // Match the known Mind+ sequence: change direction directly.
            // motorStop() is reserved for the final safe stop; it sends CW
            // with speed 0 and is not part of the direction swap.
            s_motor_failed = !motor_run(kMotorCcw) || s_motor_failed;
            Serial.printf("[D][COMPONENT] motor forward -> reverse error=%u\n",
                          (unsigned)s_motor_failed);
            s_stage = 1;
            s_last_action_ms = now_ms;
            return;
        }
        if (s_stage == 1) {
            s_motor_failed = !motor_stop() || s_motor_failed;
            Serial.printf("[D][COMPONENT] motor reverse stop error=%u\n",
                          (unsigned)s_motor_failed);
            s_stage = 2;
            s_last_action_ms = now_ms;
            return;
        }
        finish_run(s_motor_failed ? "FAIL" : "PASS");
        return;
    }

    if (s_run == RUN_IR) {
        uint16_t widths[kIrRawMax] = {};
        uint8_t levels[kIrRawMax] = {};
        uint8_t count = 0;
        copy_ir_capture(widths, levels, &count);
        uint32_t code = 0;
        if (decode_nec(widths, levels, count, &code)) {
            const char* key_name = ir_key_name(code);
            snprintf(s_rows[2].content, sizeof(s_rows[2].content),
                     "P1: %s", key_name ? key_name : "未知按键");
            snprintf(s_rows[2].result, sizeof(s_rows[2].result), "已收到");
            reset_ir_capture_buffer();
            Serial.printf("[D][COMPONENT] IR key=%s code=0x%08lX\n",
                          key_name ? key_name : "unknown",
                          (unsigned long)code);
            return;
        }
        if (count >= kIrRawMax) {
            // Noise or an unsupported protocol must not permanently fill the
            // rolling capture while the user keeps the IR test active.
            reset_ir_capture_buffer();
        }
    }
}

bool dn_component_test_is_running() {
    return s_run != RUN_NONE;
}

ComponentTestSnapshot dn_component_test_snapshot() {
    ComponentTestSnapshot out = {};
    out.rowCount = COMPONENT_TEST_MAX_ROWS;
    out.selectedIndex = s_selected_index;
    for (uint8_t i = 0; i < COMPONENT_TEST_MAX_ROWS; ++i) {
        out.rows[i] = s_rows[i];
    }
    return out;
}

} // namespace desknest
