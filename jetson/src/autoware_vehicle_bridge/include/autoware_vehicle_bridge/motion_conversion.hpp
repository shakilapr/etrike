// Copyright 2026 E-Trike
// Licensed under the Apache License, Version 2.0

#ifndef AUTOWARE_VEHICLE_BRIDGE__MOTION_CONVERSION_HPP_
#define AUTOWARE_VEHICLE_BRIDGE__MOTION_CONVERSION_HPP_

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace autoware_vehicle_bridge::motion
{

inline int32_t speed_to_mmps(double speed_mps, double max_forward, double max_reverse)
{
  const auto value = static_cast<int32_t>(std::lround(speed_mps * 1000.0));
  return std::clamp(
    value, static_cast<int32_t>(-max_reverse * 1000.0),
    static_cast<int32_t>(max_forward * 1000.0));
}

// Universe: left positive. E-Trike wire/RT: right positive.
inline double to_trike_steering_rad(double universe_angle_rad, double max_angle_rad)
{
  return -std::clamp(universe_angle_rad, -max_angle_rad, max_angle_rad);
}

inline int16_t to_trike_steering_0_1deg(double universe_angle_rad, double max_angle_rad)
{
  constexpr double kRadTo01Deg = 1800.0 / 3.14159265358979323846;
  return static_cast<int16_t>(std::lround(
    to_trike_steering_rad(universe_angle_rad, max_angle_rad) * kRadTo01Deg));
}

inline int32_t legacy_yaw_mrad_s(
  double universe_angle_rad, double speed_mps, double wheel_base,
  double max_angle_rad, double low_speed_threshold)
{
  if (std::abs(speed_mps) < low_speed_threshold) return 0;
  const double trike_angle = to_trike_steering_rad(universe_angle_rad, max_angle_rad);
  const double yaw = speed_mps * std::tan(trike_angle) / wheel_base;
  return std::clamp(static_cast<int32_t>(std::lround(yaw * 1000.0)), -3000, 3000);
}

inline float universe_heading_rate(int32_t trike_yaw_mrad_s)
{
  return static_cast<float>(-trike_yaw_mrad_s / 1000.0);
}

inline float universe_steering_rad(int16_t trike_angle_0_1deg)
{
  constexpr double k01DegToRad = 3.14159265358979323846 / 1800.0;
  return static_cast<float>(-trike_angle_0_1deg * k01DegToRad);
}

}  // namespace autoware_vehicle_bridge::motion

#endif  // AUTOWARE_VEHICLE_BRIDGE__MOTION_CONVERSION_HPP_
