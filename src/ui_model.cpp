// src/ui_model.cpp
// 栖屏 DeskNest - transition adapter from current globals to UiModel

#include "ui_model.h"

#include "sensors.h"
#include "state_machine.h"

#include <Arduino.h>

namespace desknest {

UiModel dn_build_ui_model() {
    UiModelInputs in = {};
    in.state = g_state.snapshot();
    in.nowMs = millis();

    const auto aht = g_sensors.aht20();
    in.temperatureValid = aht.valid;
    in.temperatureC = aht.temperatureC;
    in.humidityPct = aht.humidityPct;

    const auto lux = g_sensors.ltr303();
    in.luxValid = lux.valid;
    in.lux = lux.valid ? (uint16_t)lux.lux : 0;

    const auto bat = g_sensors.battery();
    in.batteryValid = bat.valid;
    in.batteryPercent = bat.percent;
    in.charging = bat.charging;

    in.shakePhase = g_gesture.shakePhase();
    in.shakeDirection = g_gesture.shakeDirection();

    return dn_build_ui_model_from_inputs(in);
}

} // namespace desknest

