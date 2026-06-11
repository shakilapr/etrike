// Motor PWM + direction — implementation.

#include "motor_driver.h"
#include "config.h"
#include <algorithm>

namespace {
constexpr int32_t kMotorEffortMax = 8191;  // 13-bit PWM (was inter_mcu::kMotorEffortMax)
}

#ifndef __cpp_lib_clamp
namespace std {
template<typename T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
}
#endif

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace sys {
namespace {
using namespace cfg;

constexpr const char* kTag     = "motor";
constexpr auto        kTimer   = LEDC_TIMER_1;
constexpr auto        kMode    = LEDC_LOW_SPEED_MODE;
constexpr auto        kChannel = LEDC_CHANNEL_1;

}

void MotorDriver::init() {
    ledc_timer_config_t tmr = {};
    tmr.speed_mode      = kMode;
    tmr.duty_resolution = LEDC_TIMER_13_BIT;
    tmr.timer_num       = kTimer;
    tmr.freq_hz         = kMotorPwmFreqHz;
    tmr.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&tmr));

    ledc_channel_config_t ch = {};
    ch.gpio_num   = kMotorPwmGpio;
    ch.speed_mode = kMode;
    ch.channel    = kChannel;
    ch.timer_sel  = kTimer;
    ch.duty       = 0;
    ch.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    gpio_set_direction(kMotorDirGpio, GPIO_MODE_OUTPUT);
    stop();
    ESP_LOGI(kTag, "PWM=%d DIR=%d @ %dHz", kMotorPwmGpio, kMotorDirGpio, kMotorPwmFreqHz);
}

void MotorDriver::set_speed(int32_t speed) {
    // Direction
    gpio_set_level(kMotorDirGpio, speed >= 0 ? 1 : 0);
    if (speed < 0) speed = -speed;

    // Duty cycle: speed / max_speed * pwm_max
    uint32_t duty = static_cast<uint64_t>(speed) * kPwmMax / kMotorMaxSpeedMmps;
    if (duty > kPwmMax) duty = kPwmMax;

    ledc_set_duty(kMode, kChannel, duty);
    ledc_update_duty(kMode, kChannel);
}

void MotorDriver::set_effort(int32_t effort_pwm) {
    effort_pwm = std::clamp(
        effort_pwm,
        -kMotorEffortMax,
        kMotorEffortMax);

    gpio_set_level(kMotorDirGpio, effort_pwm >= 0 ? 1 : 0);
    uint32_t duty = static_cast<uint32_t>(effort_pwm >= 0 ? effort_pwm : -effort_pwm);

    ledc_set_duty(kMode, kChannel, duty);
    ledc_update_duty(kMode, kChannel);
}

void MotorDriver::stop() {
    ledc_set_duty(kMode, kChannel, 0);
    ledc_update_duty(kMode, kChannel);
    gpio_set_level(kMotorDirGpio, 0);
}

}  // namespace sys
