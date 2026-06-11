// Steering servo — PWM with slew-rate limiting.

#include "steering_servo.h"
#include "config.h"
#include <algorithm>
#include <cmath>

#ifndef __cpp_lib_clamp
namespace std {
template<typename T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
}
#endif

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace rt {
namespace {

constexpr const char* kTag    = "steer";
constexpr auto        kTimer  = LEDC_TIMER_0;
constexpr auto        kMode   = LEDC_LOW_SPEED_MODE;
constexpr auto        kChannel = LEDC_CHANNEL_0;
constexpr int         kResBits = 13;
constexpr int         kDutyMax = (1 << kResBits) - 1;

}  // anonymous

uint32_t SteeringServo::angle_to_duty(int32_t mdeg) {
    int32_t lim = static_cast<int32_t>(kSteerLimitDeg * 1000.0f);
    mdeg = std::clamp(mdeg, -lim, lim);

    float ratio = static_cast<float>(mdeg + lim) / static_cast<float>(2 * lim);
    uint32_t pulse_us = kSteerServoMinUs
                      + static_cast<uint32_t>(ratio * (kSteerServoMaxUs - kSteerServoMinUs));
    uint32_t period_us = 1'000'000 / kSteerPwmFreqHz;
    return static_cast<uint32_t>(static_cast<uint64_t>(pulse_us) * kDutyMax / period_us);
}

void SteeringServo::init() {
    ledc_timer_config_t tmr = {};
    tmr.speed_mode      = kMode;
    tmr.duty_resolution = LEDC_TIMER_13_BIT;
    tmr.timer_num       = kTimer;
    tmr.freq_hz         = kSteerPwmFreqHz;
    tmr.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&tmr));

    ledc_channel_config_t ch = {};
    ch.gpio_num   = kSteerServoGpio;
    ch.speed_mode = kMode;
    ch.channel    = kChannel;
    ch.timer_sel  = kTimer;
    ch.duty       = angle_to_duty(0);
    ch.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    disable();
    ESP_LOGI(kTag, "GPIO=%d %dHz ±%.0f° slew=%.0f°/s",
             kSteerServoGpio, kSteerPwmFreqHz,
             static_cast<double>(kSteerLimitDeg),
             static_cast<double>(kSteerSlewRateDegS));
}

void SteeringServo::set_target(int32_t mdeg) {
    int32_t lim = static_cast<int32_t>(kSteerLimitDeg * 1000.0f);
    m_target_mdeg = std::clamp(mdeg, -lim, lim);
}

void SteeringServo::enable() {
    if (!m_enabled) {
        m_enabled = true;
        m_last_tick_us = esp_timer_get_time();
        ESP_LOGI(kTag, "enabled");
    }
}

void SteeringServo::disable() {
    m_enabled = false;
    ledc_set_duty(kMode, kChannel, angle_to_duty(0));
    ledc_update_duty(kMode, kChannel);
    ESP_LOGI(kTag, "disabled");
}

void SteeringServo::tick() {
    if (!m_enabled) return;

    int64_t now = esp_timer_get_time();
    float dt_s  = static_cast<float>(now - m_last_tick_us) / 1'000'000.0f;
    if (dt_s <= 0.0f) return;
    m_last_tick_us = now;

    int32_t max_step = static_cast<int32_t>(kSteerSlewRateDegS * dt_s * 1000.0f);
    int32_t err      = m_target_mdeg - m_current_mdeg;
    m_current_mdeg  += std::clamp(err, -max_step, max_step);

    uint32_t duty = angle_to_duty(m_current_mdeg);
    ledc_set_duty(kMode, kChannel, duty);
    ledc_update_duty(kMode, kChannel);
}

}  // namespace rt
