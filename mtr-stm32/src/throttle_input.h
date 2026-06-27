#pragma once
// Throttle ADC — 0-5V grip sensor via STM32 ADC.
// Adapted from sys-esp32/src/throttle_input.h.
// Architecture.md §8.6: 12-bit ADC, dead zone 200, maps to 0-3000 mm/s.
//
// STM32 HAL: ADC1_IN0 on PA0.
// Call sequence per sample:
//   HAL_ADC_Start(&hadc1);
//   HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
//   raw = HAL_ADC_GetValue(&hadc1);
//   HAL_ADC_Stop(&hadc1);

#include <cstdint>
#include "config.h"

namespace mtr {

class ThrottleInput {
public:
    /// Initialise (no-op; ADC peripheral init is via STM32CubeMX MX_ADC1_Init()).
    void init() {}

    /// Read raw ADC value from the hardware (12-bit, 0-4095).
    /// In MANUAL mode the control task calls this and feeds the result
    /// to tick() then to the DAC.
    /// STM32 HAL implementation:
    ///   extern ADC_HandleTypeDef hadc1;
    ///   HAL_ADC_Start(&hadc1);
    ///   HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    ///   uint16_t raw = static_cast<uint16_t>(HAL_ADC_GetValue(&hadc1));
    ///   HAL_ADC_Stop(&hadc1);
    ///   return raw;
    uint16_t read_raw() {
        extern ADC_HandleTypeDef hadc1;
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        uint16_t raw = static_cast<uint16_t>(HAL_ADC_GetValue(&hadc1));
        HAL_ADC_Stop(&hadc1);
        return raw;
    }

    /// Convert raw ADC value to speed in mm/s.
    /// Applies dead zone, then linear map [0..4095] → [0..kThrottleMaxSpeedMmps].
    int16_t tick(uint16_t raw_adc) {
        if (raw_adc < kThrottleDeadZone) raw_adc = 0;
        return static_cast<int16_t>(
            (static_cast<int32_t>(raw_adc) * kThrottleMaxSpeedMmps) / 4095);
    }

private:
};

/// Global throttle input instance.
extern ThrottleInput g_throttle;

}  // namespace mtr