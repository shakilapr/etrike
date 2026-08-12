#include "autoware_vehicle_bridge/motion_conversion.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"

#include <cassert>
#include <cmath>

using autoware_vehicle_bridge::motion::legacy_yaw_mrad_s;

int main()
{
  constexpr double wheel_base = 1.5;
  constexpr double max_angle = 0.698;
  constexpr double threshold = 0.05;
  constexpr double left_10deg = 0.17453292519943295;

  assert(legacy_yaw_mrad_s(left_10deg, 0.0, wheel_base, max_angle, threshold) == 0);
  assert(legacy_yaw_mrad_s(left_10deg, 0.049, wheel_base, max_angle, threshold) == 0);
  assert(legacy_yaw_mrad_s(left_10deg, -0.049, wheel_base, max_angle, threshold) == 0);
  assert(legacy_yaw_mrad_s(left_10deg, 0.05, wheel_base, max_angle, threshold) < 0);
  assert(legacy_yaw_mrad_s(left_10deg, -0.05, wheel_base, max_angle, threshold) > 0);
  assert(legacy_yaw_mrad_s(left_10deg, 0.051, wheel_base, max_angle, threshold) < 0);
  assert(legacy_yaw_mrad_s(left_10deg, -0.051, wheel_base, max_angle, threshold) > 0);

  assert(autoware_vehicle_bridge::motion::to_trike_steering_0_1deg(
    left_10deg, max_angle) == -100);
  assert(std::abs(autoware_vehicle_bridge::motion::universe_steering_rad(-100) -
    left_10deg) < 1e-6);
  assert(std::abs(autoware_vehicle_bridge::motion::universe_heading_rate(-250) -
    0.25F) < 1e-6F);

  namespace generated = etrike::protocol::generated;
  generated::HostSteerCmd steer{-100, true, 0, 7};
  etrike::protocol::Frame steer_frame;
  assert(generated::encode(steer, steer_frame) == etrike::protocol::CodecStatus::Ok);
  assert(steer_frame.id == 0x303);
  assert(steer_frame.dlc == 4);
  assert(steer_frame.data[0] == 0xFF && steer_frame.data[1] == 0x9C);
  assert(steer_frame.data[2] == 0x01 && steer_frame.data[3] == 7);

  generated::RtMotionRpt motion{1200, -85, 1, true, true, true, 0, 9};
  etrike::protocol::Frame motion_frame;
  assert(generated::encode(motion, motion_frame) == etrike::protocol::CodecStatus::Ok);
  generated::RtMotionRpt decoded{};
  assert(generated::decode(motion_frame.view(), decoded) == etrike::protocol::CodecStatus::Ok);
  assert(decoded.speed_mmps == 1200 && decoded.yaw_rate_mrad_s == -85);
  assert(decoded.gear == 1 && decoded.rolling_counter == 9);
  return 0;
}
