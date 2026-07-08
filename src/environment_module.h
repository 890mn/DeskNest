// src/environment_module.h
// 栖屏 DeskNest - environment status module
//
// 纯逻辑模块：把温湿度/光照读数转换成舒适度评分、分项等级和建议。
// 不读传感器、不画 UI，便于 host 测试与后续替换算法。

#ifndef DESKNEST_ENVIRONMENT_MODULE_H
#define DESKNEST_ENVIRONMENT_MODULE_H

#include <stdint.h>

namespace desknest {

struct EnvironmentInput {
    bool valid = false;
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    uint16_t lux = 0;
};

struct EnvironmentStatus {
    bool valid = false;
    uint8_t score = 0;
    const char* gradeText = "欠佳";
    const char* temperatureGrade = "--";
    const char* humidityGrade = "--";
    const char* lightGrade = "--";
    const char* adviceText = "等待传感器";
};

inline const char* dn_environment_grade_from_score(uint8_t score) {
    if (score >= 80) return "良好";
    if (score >= 60) return "一般";
    return "欠佳";
}

inline EnvironmentStatus dn_evaluate_environment(const EnvironmentInput& in) {
    EnvironmentStatus out;
    out.valid = in.valid;

    if (!in.valid) {
        return out;
    }

    if (in.temperatureC >= 22 && in.temperatureC <= 26) {
        out.score += 35;
        out.temperatureGrade = "OK";
    } else if (in.temperatureC >= 18 && in.temperatureC < 22) {
        out.score += 25;
        out.temperatureGrade = "偏冷";
    } else if (in.temperatureC > 26 && in.temperatureC <= 30) {
        out.score += 25;
        out.temperatureGrade = "偏热";
    } else if (in.temperatureC < 18) {
        out.score += 15;
        out.temperatureGrade = "偏冷";
    } else {
        out.score += 10;
        out.temperatureGrade = "偏热";
    }

    if (in.humidityPct >= 40 && in.humidityPct <= 60) {
        out.score += 35;
        out.humidityGrade = "OK";
    } else if (in.humidityPct >= 30 && in.humidityPct < 40) {
        out.score += 25;
        out.humidityGrade = "干燥";
    } else if (in.humidityPct > 60 && in.humidityPct <= 70) {
        out.score += 25;
        out.humidityGrade = "潮湿";
    } else if (in.humidityPct < 30) {
        out.score += 15;
        out.humidityGrade = "干燥";
    } else {
        out.score += 15;
        out.humidityGrade = "潮湿";
    }

    if (in.lux >= 200 && in.lux <= 600) {
        out.score += 30;
        out.lightGrade = "OK";
    } else if (in.lux >= 100 && in.lux < 200) {
        out.score += 22;
        out.lightGrade = "偏暗";
    } else if (in.lux > 600 && in.lux <= 800) {
        out.score += 22;
        out.lightGrade = "偏亮";
    } else if (in.lux < 100) {
        out.score += 12;
        out.lightGrade = "偏暗";
    } else {
        out.score += 12;
        out.lightGrade = "偏亮";
    }

    out.gradeText = dn_environment_grade_from_score(out.score);

    if (out.score >= 80) {
        out.adviceText = "保持专注";
    } else if (out.temperatureGrade[0] != 'O' && out.temperatureGrade[0] != '-') {
        out.adviceText = (in.temperatureC < 22) ? "注意保暖" : "建议通风";
    } else if (out.humidityGrade[0] != 'O' && out.humidityGrade[0] != '-') {
        out.adviceText = (in.humidityPct < 40) ? "建议加湿" : "建议除湿";
    } else if (out.lightGrade[0] != 'O' && out.lightGrade[0] != '-') {
        out.adviceText = (in.lux < 200) ? "建议补光" : "柔和光源";
    } else {
        out.adviceText = "微调环境";
    }

    return out;
}

} // namespace desknest

#endif // DESKNEST_ENVIRONMENT_MODULE_H
