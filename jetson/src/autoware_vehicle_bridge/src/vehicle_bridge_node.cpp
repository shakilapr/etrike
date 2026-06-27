// Copyright 2026 E-Trike
// Licensed under the Apache License, Version 2.0

#include "autoware_vehicle_bridge/vehicle_bridge_node.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace autoware_vehicle_bridge
{

// =====================================================================
//  CAN ID constants
// =====================================================================
constexpr canid_t CAN_ESTOP        = 0x001;
constexpr canid_t CAN_SAFETY_STS   = 0x011;
constexpr canid_t CAN_THROTTLE_STS = 0x120;
constexpr canid_t CAN_STATE_RPT    = 0x210;
constexpr canid_t CAN_DRIVE_CMD    = 0x300;
constexpr canid_t CAN_BRAKE_REQ    = 0x301;
constexpr canid_t CAN_LIGHT_CMD    = 0x302;
constexpr canid_t CAN_DIAG_RPT     = 0x600;
constexpr canid_t CAN_MOTOR_FBK   = 0x206;
constexpr canid_t CAN_HOST_HB     = 0x7FC;
constexpr canid_t CAN_RT_HB        = 0x7FD;

// =====================================================================
//  Gear constants (Autoware.Auto <-> CAN)
// =====================================================================
namespace gear {
  // Autoware.Auto GearCommand/GearReport
  constexpr uint8_t NONE    = 0;
  constexpr uint8_t DRIVE   = 1;
  constexpr uint8_t REVERSE = 20;
  constexpr uint8_t PARK    = 22;
  constexpr uint8_t LOW     = 23;
  // CAN bus gear (0x300 byte 7)
  constexpr uint8_t CAN_N = 0, CAN_D = 1, CAN_S = 2, CAN_R = 3;
}

namespace mode {
  constexpr uint8_t AUTONOMOUS  = 1;
  constexpr uint8_t MANUAL      = 4;
  constexpr uint8_t DISENGAGED  = 5;
}

// =====================================================================
//  VehicleParams
// =====================================================================
bool VehicleParams::load_from(const rclcpp::Node * node)
{
  wheel_base              = node->get_parameter("wheel_base").as_double();
  max_speed_forward       = node->get_parameter("max_speed_forward").as_double();
  max_speed_reverse       = node->get_parameter("max_speed_reverse").as_double();
  max_steering_angle      = node->get_parameter("max_steering_angle").as_double();
  max_brake_pressure_kpa  = node->get_parameter("max_brake_pressure_kpa").as_double();
  max_deceleration        = node->get_parameter("max_deceleration").as_double();
  low_speed_threshold     = node->get_parameter("low_speed_threshold").as_double();
  loop_rate               = node->get_parameter("loop_rate").as_double();
  command_timeout_ms      = node->get_parameter("command_timeout_ms").as_int();
  heartbeat_interval_ms   = node->get_parameter("heartbeat_interval_ms").as_int();
  rt_heartbeat_timeout_ms = node->get_parameter("rt_heartbeat_timeout_ms").as_int();
  can_interface           = node->get_parameter("can_interface").as_string();
  return true;
}

void VehicleParams::validate_or_throw() const
{
  if (wheel_base <= 0.0)          throw std::domain_error("wheel_base must be positive");
  if (max_speed_forward <= 0.0)   throw std::domain_error("max_speed_forward must be positive");
  if (max_steering_angle <= 0.0)  throw std::domain_error("max_steering_angle must be positive");
  if (max_brake_pressure_kpa <= 0) throw std::domain_error("max_brake_pressure_kpa must be positive");
  if (max_deceleration <= 0.0)    throw std::domain_error("max_deceleration must be positive");
  if (loop_rate <= 0.0)           throw std::domain_error("loop_rate must be positive");
  if (command_timeout_ms <= 0)    throw std::domain_error("command_timeout_ms must be positive");
  if (can_interface.empty())      throw std::domain_error("can_interface must not be empty");
}

// =====================================================================
//  SocketCanDriver
// =====================================================================
bool SocketCanDriver::open(const std::string & interface)
{
  close();
  fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd_ < 0) return false;

  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);
  if (ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) { close(); return false; }

  struct sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) { close(); return false; }

  return true;
}

void SocketCanDriver::close()
{
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool SocketCanDriver::send(const struct can_frame & frame)
{
  if (fd_ < 0) return false;
  return write(fd_, &frame, sizeof(frame)) == static_cast<int>(sizeof(frame));
}

bool SocketCanDriver::receive(struct can_frame & frame, int timeout_ms)
{
  if (fd_ < 0) return false;
  if (timeout_ms > 0) {
    struct timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }
  return read(fd_, &frame, sizeof(frame)) == static_cast<int>(sizeof(frame));
}

// =====================================================================
//  CanEncoder
// =====================================================================
CanEncoder::CanEncoder(const VehicleParams & params) : params_(params) {}

int32_t CanEncoder::speed_to_mmps(float speed_ms) const
{
  int32_t mmps = static_cast<int32_t>(speed_ms * 1000.0f);
  return std::clamp(mmps,
    static_cast<int32_t>(-params_.max_speed_reverse * 1000.0f),
    static_cast<int32_t>(params_.max_speed_forward * 1000.0f));
}

int32_t CanEncoder::steering_to_yaw(float angle_rad, float speed_ms) const
{
  if (speed_ms < params_.low_speed_threshold) return 0;
  float omega = speed_ms * std::tan(angle_rad) / params_.wheel_base;
  return std::clamp(static_cast<int32_t>(omega * 1000.0f), -3000, 3000);
}

uint8_t CanEncoder::derive_gear(int32_t speed_mmps, uint8_t gear_override, bool has_override) const
{
  if (has_override) return gear_override;
  if (speed_mmps > 50)  return gear::CAN_D;
  if (speed_mmps < -50) return gear::CAN_R;
  return gear::CAN_N;
}

bool CanEncoder::encode_drive(const autoware_auto_control_msgs::msg::AckermannControlCommand & cmd,
                              uint8_t gear_override, bool has_override,
                              struct can_frame & frame)
{
  std::memset(&frame, 0, sizeof(frame));
  frame.can_id = CAN_DRIVE_CMD;
  frame.len = 8;

  float speed_ms = cmd.longitudinal.is_defined_speed ? cmd.longitudinal.speed : 0.0f;
  int32_t speed_mmps = speed_to_mmps(speed_ms);

  float v_abs = std::abs(speed_ms);
  float steer = cmd.lateral.is_defined_steering_tire_angle ? cmd.lateral.steering_tire_angle : 0.0f;
  int32_t yaw_mrad = steering_to_yaw(steer, v_abs);

  uint8_t gear = derive_gear(speed_mmps, gear_override, has_override);

  // big-endian
  frame.data[0] = (speed_mmps >> 24) & 0xFF;
  frame.data[1] = (speed_mmps >> 16) & 0xFF;
  frame.data[2] = (speed_mmps >> 8)  & 0xFF;
  frame.data[3] = speed_mmps & 0xFF;
  frame.data[4] = (yaw_mrad >> 16) & 0xFF;
  frame.data[5] = (yaw_mrad >> 8)  & 0xFF;
  frame.data[6] = yaw_mrad & 0xFF;
  frame.data[7] = gear;
  return true;
}

bool CanEncoder::encode_brake(const autoware_auto_control_msgs::msg::AckermannControlCommand & cmd,
                              struct can_frame & frame)
{
  if (!cmd.longitudinal.is_defined_acceleration) return false;

  float accel = cmd.longitudinal.acceleration;
  int32_t kpa = 0;
  if (accel < 0.0f) {
    float decel = -accel;
    kpa = static_cast<int32_t>((decel / params_.max_deceleration) * params_.max_brake_pressure_kpa);
    kpa = std::clamp(kpa, 0, static_cast<int32_t>(params_.max_brake_pressure_kpa));
  }

  if (kpa == last_brake_kpa_) return false;  // no change
  last_brake_kpa_ = kpa;

  std::memset(&frame, 0, sizeof(frame));
  frame.can_id = CAN_BRAKE_REQ;
  frame.len = 4;
  frame.data[0] = (kpa >> 24) & 0xFF;
  frame.data[1] = (kpa >> 16) & 0xFF;
  frame.data[2] = (kpa >> 8)  & 0xFF;
  frame.data[3] = kpa & 0xFF;
  return true;
}

bool CanEncoder::encode_lights(const autoware_auto_vehicle_msgs::msg::TurnIndicatorsCommand * turn,
                               const autoware_auto_vehicle_msgs::msg::HazardLightsCommand * hazard,
                               bool is_braking,
                               struct can_frame & frame)
{
  uint8_t bits = 0;
  if (turn) {
    if (turn->command == autoware_auto_vehicle_msgs::msg::TurnIndicatorsCommand::ENABLE_LEFT)  bits |= 0x01;
    if (turn->command == autoware_auto_vehicle_msgs::msg::TurnIndicatorsCommand::ENABLE_RIGHT) bits |= 0x02;
  }
  if (hazard && hazard->command == autoware_auto_vehicle_msgs::msg::HazardLightsCommand::ENABLE) bits |= 0x03;
  if (is_braking) bits |= 0x04;
  // bit3=headlight reserved for future

  if (bits == last_light_bits_) return false;
  last_light_bits_ = bits;

  std::memset(&frame, 0, sizeof(frame));
  frame.can_id = CAN_LIGHT_CMD;
  frame.len = 1;
  frame.data[0] = bits;
  return true;
}

bool CanEncoder::encode_heartbeat(struct can_frame & frame)
{
  std::memset(&frame, 0, sizeof(frame));
  frame.can_id = CAN_HOST_HB;
  frame.len = 1;
  frame.data[0] = host_alive_ctr_++;
  return true;
}

bool CanEncoder::encode_estop(struct can_frame & frame)
{
  std::memset(&frame, 0, sizeof(frame));
  frame.can_id = CAN_ESTOP;
  frame.len = 0;
  return true;
}

// =====================================================================
//  CanDecoder
// =====================================================================
bool CanDecoder::decode_velocity(const struct can_frame & frame,
                                 autoware_auto_vehicle_msgs::msg::VelocityReport & msg) const
{
  if (frame.len < 2) return false;
  int16_t mmps = static_cast<int16_t>((static_cast<uint16_t>(frame.data[0]) << 8) | frame.data[1]);
  msg.longitudinal_velocity = mmps / 1000.0f;
  msg.lateral_velocity = 0.0f;
  msg.heading_rate = 0.0f;
  return true;
}

bool CanDecoder::decode_steering(const struct can_frame & frame,
                                 autoware_auto_vehicle_msgs::msg::SteeringReport & msg) const
{
  (void)frame;
  // Steering feedback not yet available on high bus (EPS-C 0x201 is low-bus only).
  // RT_STATE_RPT byte 1 gives steer_valid flag only.
  msg.steering_tire_angle = 0.0f;
  return true;
}

bool CanDecoder::decode_state(const struct can_frame & frame,
                              autoware_auto_vehicle_msgs::msg::ControlModeReport & mode_msg,
                              autoware_auto_vehicle_msgs::msg::GearReport & gear_msg) const
{
  if (frame.len < 3) return false;
  uint8_t trike_mode = frame.data[0];
  bool reversing = (frame.data[2] != 0);

  switch (trike_mode) {
    case 0: mode_msg.mode = mode::MANUAL;     break;
    case 1: mode_msg.mode = mode::AUTONOMOUS; break;
    default: mode_msg.mode = mode::DISENGAGED; break;
  }

  uint8_t can_gear = reversing ? gear::CAN_R : gear::CAN_D;
  switch (can_gear) {
    case gear::CAN_D: gear_msg.report = gear::DRIVE;   break;
    case gear::CAN_S: gear_msg.report = gear::LOW;     break;
    case gear::CAN_R: gear_msg.report = gear::REVERSE; break;
    default:          gear_msg.report = gear::NONE;     break;
  }
  return true;
}

bool CanDecoder::decode_diagnostics(const struct can_frame & frame,
                                    diagnostic_msgs::msg::DiagnosticArray & msg,
                                    const rclcpp::Time & now) const
{
  if (frame.len < 8) return false;
  msg.header.stamp = now;
  msg.status.clear();

  auto add = [&](const std::string & name, uint8_t value, uint8_t warn, uint8_t err) {
    diagnostic_msgs::msg::DiagnosticStatus s;
    s.name = name;
    s.hardware_id = "etrike";
    s.values = {{"raw", std::to_string(value)}};
    s.level = (value >= err) ? diagnostic_msgs::msg::DiagnosticStatus::ERROR
            : (value >= warn) ? diagnostic_msgs::msg::DiagnosticStatus::WARN
            : diagnostic_msgs::msg::DiagnosticStatus::OK;
    s.message = s.level == diagnostic_msgs::msg::DiagnosticStatus::OK ? "OK" : "WARNING";
    msg.status.push_back(s);
  };

  add("mode", frame.data[0], 0, 0);
  add("brake_engaged", frame.data[1], 0, 0);
  add("heartbeat_ok", frame.data[2], 1, 1);
  add("estop_active", frame.data[3], 1, 1);
  add("tec", frame.data[6], 96, 128);
  add("rec", frame.data[7], 96, 128);
  return true;
}

bool CanDecoder::validate_heartbeat(const struct can_frame & frame,
                                    uint8_t & last_ctr, rclcpp::Time & last_time,
                                    const rclcpp::Time & now, int timeout_ms) const
{
  if (frame.len < 1) return false;
  uint8_t ctr = frame.data[0];
  if (ctr != last_ctr) {
    last_ctr = ctr;
    last_time = now;
    return true;
  }
  // Same counter = frozen controller
  return (now - last_time).seconds() * 1000.0 < timeout_ms;
}

// =====================================================================
//  HeartbeatMonitor
// =====================================================================
void HeartbeatMonitor::feed(uint8_t counter, const rclcpp::Time & now)
{
  std::lock_guard<std::mutex> lk(mutex_);
  if (counter != counter_) {
    counter_ = counter;
    last_time_ = now;
  }
}

bool HeartbeatMonitor::is_alive(const rclcpp::Time & now, int timeout_ms) const
{
  std::lock_guard<std::mutex> lk(mutex_);
  if (last_time_.nanoseconds() == 0) return true;  // no data yet
  return (now - last_time_).seconds() * 1000.0 < timeout_ms;
}

// =====================================================================
//  VehicleBridgeNode
// =====================================================================
VehicleBridgeNode::VehicleBridgeNode(const rclcpp::NodeOptions & options)
: rclcpp_lifecycle::LifecycleNode("vehicle_bridge", options)
{
  using autoware_auto_control_msgs::msg::AckermannControlCommand;
  using autoware_auto_vehicle_msgs::msg::ControlModeCommand;
  using autoware_auto_vehicle_msgs::msg::Engage;
  using autoware_auto_vehicle_msgs::msg::GearCommand;
  using autoware_auto_vehicle_msgs::msg::HazardLightsCommand;
  using autoware_auto_vehicle_msgs::msg::TurnIndicatorsCommand;
  using autoware_auto_vehicle_msgs::msg::VehicleKinematicState;
  using tier4_vehicle_msgs::msg::VehicleEmergencyStamped;

  declare_parameter("wheel_base", 1.5);
  declare_parameter("max_speed_forward", 3.0);
  declare_parameter("max_speed_reverse", 0.5);
  declare_parameter("max_steering_angle", 0.698);
  declare_parameter("max_brake_pressure_kpa", 5000.0);
  declare_parameter("max_deceleration", 5.0);
  declare_parameter("low_speed_threshold", 0.05);
  declare_parameter("loop_rate", 100.0);
  declare_parameter("command_timeout_ms", 500);
  declare_parameter("heartbeat_interval_ms", 500);
  declare_parameter("rt_heartbeat_timeout_ms", 1500);
  declare_parameter("can_interface", "can0");

  sub_control_ = create_subscription<AckermannControlCommand>("~/input/control_cmd", rclcpp::QoS(1),
    [this](const AckermannControlCommand::SharedPtr m) { on_control(m); });
  sub_gear_ = create_subscription<GearCommand>("~/input/gear_cmd", rclcpp::QoS(1),
    [this](const GearCommand::SharedPtr m) { on_gear(m); });
  sub_turn_ = create_subscription<TurnIndicatorsCommand>("~/input/turn_indicators_cmd", rclcpp::QoS(1),
    [this](const TurnIndicatorsCommand::SharedPtr m) { on_turn(m); });
  sub_hazard_ = create_subscription<HazardLightsCommand>("~/input/hazard_lights_cmd", rclcpp::QoS(1),
    [this](const HazardLightsCommand::SharedPtr m) { on_hazard(m); });
  sub_engage_ = create_subscription<Engage>("~/input/engage", rclcpp::QoS(1),
    [this](const Engage::SharedPtr m) { on_engage(m); });
  sub_control_mode_ = create_subscription<ControlModeCommand>("~/input/control_mode", rclcpp::QoS(1),
    [this](const ControlModeCommand::SharedPtr m) { on_control_mode(m); });
  sub_emergency_ = create_subscription<VehicleEmergencyStamped>("~/input/emergency_cmd", rclcpp::QoS(1),
    [this](const VehicleEmergencyStamped::SharedPtr m) { on_emergency(m); });

  pub_velocity_      = create_publisher<autoware_auto_vehicle_msgs::msg::VelocityReport>("~/output/velocity_status", rclcpp::QoS(1));
  pub_steering_      = create_publisher<autoware_auto_vehicle_msgs::msg::SteeringReport>("~/output/steering_status", rclcpp::QoS(1));
  pub_gear_          = create_publisher<autoware_auto_vehicle_msgs::msg::GearReport>("~/output/gear_status", rclcpp::QoS(1));
  pub_mode_          = create_publisher<autoware_auto_vehicle_msgs::msg::ControlModeReport>("~/output/control_mode", rclcpp::QoS(1));
  pub_turn_status_   = create_publisher<autoware_auto_vehicle_msgs::msg::TurnIndicatorsReport>("~/output/turn_indicators_status", rclcpp::QoS(1));
  pub_hazard_status_ = create_publisher<autoware_auto_vehicle_msgs::msg::HazardLightsReport>("~/output/hazard_lights_status", rclcpp::QoS(1));
  pub_kinematic_state_ = create_publisher<autoware_auto_vehicle_msgs::msg::VehicleKinematicState>("~/output/kinematic_state", rclcpp::QoS(1));
  pub_diag_          = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("~/output/diagnostics", rclcpp::QoS(1));

  can_ = std::make_unique<SocketCanDriver>();
  RCLCPP_INFO(get_logger(), "VehicleBridgeNode constructed.");
}

VehicleBridgeNode::~VehicleBridgeNode() { can_->close(); }

void VehicleBridgeNode::set_can_driver(std::unique_ptr<CanDriver> driver) { can_ = std::move(driver); }

// ---- Lifecycle ----
CallbackReturn VehicleBridgeNode::on_configure(const State &)
{
  RCLCPP_INFO(get_logger(), "on_configure");
  if (!load_parameters()) return CallbackReturn::FAILURE;
  try { params_.validate_or_throw(); }
  catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(), "Parameter validation failed: %s", e.what());
    return CallbackReturn::FAILURE;
  }
  if (!can_->open(params_.can_interface)) {
    RCLCPP_ERROR(get_logger(), "Failed to open CAN '%s': %s", params_.can_interface.c_str(), std::strerror(errno));
    return CallbackReturn::FAILURE;
  }

  encoder_ = std::make_unique<CanEncoder>(params_);
  decoder_ = std::make_unique<CanDecoder>();

  auto loop = std::chrono::duration<double>(1.0 / params_.loop_rate);
  timer_control_ = create_wall_timer(
    std::chrono::duration_cast<std::chrono::nanoseconds>(loop),
    std::bind(&VehicleBridgeNode::tick_control, this));
  timer_control_->cancel();

  timer_heartbeat_ = create_wall_timer(
    std::chrono::milliseconds(params_.heartbeat_interval_ms),
    std::bind(&VehicleBridgeNode::tick_heartbeat, this));
  timer_heartbeat_->cancel();

  timer_diag_ = create_wall_timer(
    std::chrono::seconds(1),
    std::bind(&VehicleBridgeNode::tick_diagnostics, this));
  timer_diag_->cancel();

  RCLCPP_INFO(get_logger(), "Configured: wheelbase=%.2f loop=%.0fHz can=%s",
    params_.wheel_base, params_.loop_rate, params_.can_interface.c_str());
  return CallbackReturn::SUCCESS;
}

CallbackReturn VehicleBridgeNode::on_activate(const State &)
{
  RCLCPP_INFO(get_logger(), "on_activate");
  // Reopen CAN socket (was closed in on_deactivate)
  if (!can_->is_open() && !can_->open(params_.can_interface)) {
    RCLCPP_ERROR(get_logger(), "Failed to reopen CAN '%s'", params_.can_interface.c_str());
    return CallbackReturn::FAILURE;
  }

  pub_velocity_->on_activate();
  pub_steering_->on_activate();
  pub_gear_->on_activate();
  pub_mode_->on_activate();
  pub_turn_status_->on_activate();
  pub_hazard_status_->on_activate();
  pub_kinematic_state_->on_activate();
  pub_diag_->on_activate();

  timer_control_->reset();
  timer_heartbeat_->reset();
  timer_diag_->reset();
  last_cmd_time_ = now();

  rx_running_ = true;
  rx_thread_ = std::thread(&VehicleBridgeNode::run_can_receive, this);
  return CallbackReturn::SUCCESS;
}

CallbackReturn VehicleBridgeNode::on_deactivate(const State &)
{
  RCLCPP_INFO(get_logger(), "on_deactivate");
  timer_control_->cancel();
  timer_heartbeat_->cancel();
  timer_diag_->cancel();

  rx_running_ = false;
  can_->close();
  if (rx_thread_.joinable()) rx_thread_.join();

  pub_velocity_->on_deactivate();
  pub_steering_->on_deactivate();
  pub_gear_->on_deactivate();
  pub_mode_->on_deactivate();
  pub_turn_status_->on_deactivate();
  pub_hazard_status_->on_deactivate();
  pub_kinematic_state_->on_deactivate();
  pub_diag_->on_deactivate();
  return CallbackReturn::SUCCESS;
}

CallbackReturn VehicleBridgeNode::on_cleanup(const State &)
{
  RCLCPP_INFO(get_logger(), "on_cleanup");
  can_->close();
  timer_control_.reset();
  timer_heartbeat_.reset();
  timer_diag_.reset();
  encoder_.reset();
  decoder_.reset();
  engaged_ = false;
  rt_heartbeat_ = HeartbeatMonitor{};
  return CallbackReturn::SUCCESS;
}

CallbackReturn VehicleBridgeNode::on_shutdown(const State &)
{
  RCLCPP_INFO(get_logger(), "on_shutdown");
  rx_running_ = false;
  can_->close();
  if (rx_thread_.joinable()) rx_thread_.join();
  return CallbackReturn::SUCCESS;
}

// ---- Parameter loading ----
bool VehicleBridgeNode::load_parameters()
{
  try { params_.load_from(this); }
  catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(), "Parameter load failed: %s", e.what());
    return false;
  }
  return true;
}

// ---- Subscription callbacks ----
void VehicleBridgeNode::on_control(const autoware_auto_control_msgs::msg::AckermannControlCommand::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latest_control_ = msg;
  last_cmd_time_ = now();
}

void VehicleBridgeNode::on_gear(const autoware_auto_vehicle_msgs::msg::GearCommand::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latest_gear_ = msg;
}

void VehicleBridgeNode::on_turn(const autoware_auto_vehicle_msgs::msg::TurnIndicatorsCommand::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latest_turn_ = msg;
}

void VehicleBridgeNode::on_hazard(const autoware_auto_vehicle_msgs::msg::HazardLightsCommand::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latest_hazard_ = msg;
}

void VehicleBridgeNode::on_engage(const autoware_auto_vehicle_msgs::msg::Engage::SharedPtr msg)
{
  engaged_ = msg->engage;
  RCLCPP_INFO(get_logger(), "Engage: %s", engaged_ ? "ON" : "OFF");
}

void VehicleBridgeNode::on_control_mode(const autoware_auto_vehicle_msgs::msg::ControlModeCommand::SharedPtr msg)
{
  // Map ControlModeCommand to Engage — physical mode gated by SYS MODE button
  engaged_ = (msg->mode == autoware_auto_vehicle_msgs::msg::ControlModeCommand::AUTONOMOUS);
  RCLCPP_INFO(get_logger(), "ControlMode request: %s", engaged_ ? "AUTONOMOUS" : "MANUAL");
}

void VehicleBridgeNode::on_emergency(const tier4_vehicle_msgs::msg::VehicleEmergencyStamped::SharedPtr /*msg*/)
{
  // Rate-limited: max 1 ESTOP frame per 500ms from Host
  auto n = now();
  if ((n - last_estop_tx_).seconds() * 1000.0 < 500.0) return;
  last_estop_tx_ = n;

  RCLCPP_ERROR(get_logger(), "EMERGENCY received — sending ESTOP");
  struct can_frame frame;
  if (encoder_->encode_estop(frame) && can_->is_open())
    can_->send(frame);
}

// ---- Timer ticks ----
void VehicleBridgeNode::tick_control()
{
  if (!can_->is_open()) return;

  auto cmd_age = (now() - last_cmd_time_).seconds() * 1000.0;
  if (cmd_age > params_.command_timeout_ms) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "Command timeout: %.0fms — sending zero speed", cmd_age);
    // Send zero-speed frame so RT's staleness watchdog doesn't need to wait 500ms
    struct can_frame z;
    std::memset(&z, 0, sizeof(z));
    z.can_id = CAN_DRIVE_CMD; z.len = 8;
    z.data[7] = gear::CAN_N;  // all zeros = speed 0, yaw 0, gear N
    can_->send(z);
    return;
  }
  if (!engaged_) return;

  // Snapshot latest commands
  autoware_auto_control_msgs::msg::AckermannControlCommand::SharedPtr ctrl;
  autoware_auto_vehicle_msgs::msg::GearCommand::SharedPtr gear;
  autoware_auto_vehicle_msgs::msg::TurnIndicatorsCommand::SharedPtr turn;
  autoware_auto_vehicle_msgs::msg::HazardLightsCommand::SharedPtr hazard;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ctrl = latest_control_; gear = latest_gear_; turn = latest_turn_; hazard = latest_hazard_;
  }
  if (!ctrl) return;

  // Gear override
  uint8_t gear_val = gear::CAN_N;
  bool has_gear = false;
  if (gear && gear->command != autoware_auto_vehicle_msgs::msg::GearCommand::NO_COMMAND) {
    switch (gear->command) {
      case gear::DRIVE:   gear_val = gear::CAN_D; break;
      case gear::REVERSE: gear_val = gear::CAN_R; break;
      case gear::LOW:     gear_val = gear::CAN_S; break;
      default:            gear_val = gear::CAN_N; break;
    }
    has_gear = true;
  }

  struct can_frame frame;

  // 0x300 HOST_DRIVE_CMD
  if (encoder_->encode_drive(*ctrl, gear_val, has_gear, frame))
    can_->send(frame);

  // 0x301 HOST_BRAKE_REQ
  if (encoder_->encode_brake(*ctrl, frame))
    can_->send(frame);

  // 0x302 HOST_LIGHT_CMD
  bool braking = ctrl->longitudinal.is_defined_acceleration && ctrl->longitudinal.acceleration < 0.0f;
  if (encoder_->encode_lights(turn.get(), hazard.get(), braking, frame))
    can_->send(frame);

  // Turn/hazard status published from 0x011 CAN feedback (actual state, not echo)
}

void VehicleBridgeNode::tick_heartbeat()
{
  if (!can_->is_open()) return;

  // Monitor RT heartbeat
  if (!rt_heartbeat_.is_alive(now(), params_.rt_heartbeat_timeout_ms)) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "RT heartbeat LOST");
  }

  // Send Host heartbeat (0x7FC)
  struct can_frame frame;
  if (encoder_->encode_heartbeat(frame))
    can_->send(frame);
}

void VehicleBridgeNode::tick_diagnostics()
{
  if (!pub_diag_->is_activated()) return;
  diagnostic_msgs::msg::DiagnosticArray diag;
  diag.header.stamp = now();

  auto add = [&](const std::string & name, bool ok, const std::string & detail) {
    diagnostic_msgs::msg::DiagnosticStatus s;
    s.name = name;
    s.hardware_id = "etrike";
    s.level = ok ? diagnostic_msgs::msg::DiagnosticStatus::OK : diagnostic_msgs::msg::DiagnosticStatus::WARN;
    s.message = detail;
    diag.status.push_back(s);
  };

  add("CAN", can_->is_open(), can_->is_open() ? "connected" : "disconnected");
  add("Engage", engaged_, engaged_ ? "engaged" : "disengaged");
  add("RT Heartbeat", rt_heartbeat_.is_alive(now(), params_.rt_heartbeat_timeout_ms),
      rt_heartbeat_.is_alive(now(), params_.rt_heartbeat_timeout_ms) ? "alive" : "timeout");
  uint8_t hb = sys_heartbeat_ok_.load(std::memory_order_relaxed);
  uint8_t estop = sys_estop_active_.load(std::memory_order_relaxed);
  add("SYS Heartbeat", hb == 1, hb ? "alive" : "timeout");
  add("SYS ESTOP", estop == 0, estop ? "ACTIVE" : "clear");

  pub_diag_->publish(diag);
}

// ---- CAN receive thread ----
void VehicleBridgeNode::run_can_receive()
{
  RCLCPP_INFO(get_logger(), "CAN RX thread started");
  while (rx_running_) {
    struct can_frame frame;
    if (!can_->receive(frame, 100)) continue;
    publish_vehicle_reports(frame);
  }
  RCLCPP_INFO(get_logger(), "CAN RX thread stopped");
}

void VehicleBridgeNode::publish_vehicle_reports(const struct can_frame & frame)
{
  switch (frame.can_id) {
    case CAN_ESTOP:
      RCLCPP_WARN(get_logger(), "ESTOP received (DLC=%d)", frame.len);
      break;

    case CAN_THROTTLE_STS: {
      autoware_auto_vehicle_msgs::msg::VelocityReport vel;
      if (decoder_->decode_velocity(frame, vel) && pub_velocity_->is_activated())
        pub_velocity_->publish(vel);

      // Dead-reckoning odometry: integrate speed + steer angle via tricycle model
      auto n = now();
      if ((n - last_steer_time_).seconds() < 0.2 && last_odom_time_.nanoseconds() > 0) {
        double dt = (n - last_odom_time_).seconds();
        if (dt > 0.0 && dt < 0.5) {
          double v = vel.longitudinal_velocity;
          double omega = (std::abs(v) > params_.low_speed_threshold)
            ? v * std::tan(steer_angle_rad_) / params_.wheel_base
            : 0.0;
          odom_yaw_ += omega * dt;
          odom_x_ += v * std::cos(odom_yaw_) * dt;
          odom_y_ += v * std::sin(odom_yaw_) * dt;

          autoware_auto_vehicle_msgs::msg::VehicleKinematicState kine;
          kine.header.stamp = n;
          kine.header.frame_id = "base_link";
          kine.state.pose.position.x = odom_x_;
          kine.state.pose.position.y = odom_y_;
          // Quaternion from yaw (rotation about Z)
          kine.state.pose.orientation.z = std::sin(odom_yaw_ * 0.5);
          kine.state.pose.orientation.w = std::cos(odom_yaw_ * 0.5);
          kine.state.twist.linear.x = v;
          kine.state.twist.angular.z = omega;
          if (pub_kinematic_state_->is_activated()) pub_kinematic_state_->publish(kine);
        }
      }
      last_odom_time_ = n;
      break;
    }

    case CAN_MOTOR_FBK: {  // 0x206 — actual gear state from MTR (forwarded low→high)
      if (frame.len < 3) break;
      autoware_auto_vehicle_msgs::msg::GearReport gear;
      switch (frame.data[2]) {  // MTR_GearState
        case gear::CAN_N: gear.report = gear::NONE;    break;
        case gear::CAN_D: gear.report = gear::DRIVE;   break;
        case gear::CAN_S: gear.report = gear::LOW;     break;
        case gear::CAN_R: gear.report = gear::REVERSE; break;
        default:          gear.report = gear::NONE;     break;
      }
      if (pub_gear_->is_activated()) pub_gear_->publish(gear);
      break;
    }

    case CAN_SAFETY_STS: {  // 0x011 — SYS liveness + light state (forwarded low→high)
      if (frame.len < 2) break;
      sys_estop_active_.store(frame.data[0], std::memory_order_relaxed);
      sys_heartbeat_ok_.store(frame.data[1], std::memory_order_relaxed);

      // Light state feedback (present when DLC ≥ 3, v0.0.5)
      if (frame.len >= 3) {
        uint8_t lights = frame.data[2];
        autoware_auto_vehicle_msgs::msg::TurnIndicatorsReport turn;
        if ((lights & 0x03) == 0x03)
          turn.report = autoware_auto_vehicle_msgs::msg::TurnIndicatorsReport::DISABLE;  // hazard: both
        else if (lights & 0x01)
          turn.report = autoware_auto_vehicle_msgs::msg::TurnIndicatorsReport::ENABLE_LEFT;
        else if (lights & 0x02)
          turn.report = autoware_auto_vehicle_msgs::msg::TurnIndicatorsReport::ENABLE_RIGHT;
        else
          turn.report = autoware_auto_vehicle_msgs::msg::TurnIndicatorsReport::DISABLE;
        if (pub_turn_status_->is_activated()) pub_turn_status_->publish(turn);

        autoware_auto_vehicle_msgs::msg::HazardLightsReport hazard;
        hazard.report = ((lights & 0x03) == 0x03)
          ? autoware_auto_vehicle_msgs::msg::HazardLightsReport::ENABLE
          : autoware_auto_vehicle_msgs::msg::HazardLightsReport::DISABLE;
        if (pub_hazard_status_->is_activated()) pub_hazard_status_->publish(hazard);
      }
      break;
    }

    case CAN_STATE_RPT: {
      autoware_auto_vehicle_msgs::msg::ControlModeReport mode;
      autoware_auto_vehicle_msgs::msg::GearReport gear;
      if (decoder_->decode_state(frame, mode, gear)) {
        if (pub_mode_->is_activated()) pub_mode_->publish(mode);
        if (pub_gear_->is_activated()) pub_gear_->publish(gear);
      }
      break;
    }

    case CAN_DIAG_RPT: {
      diagnostic_msgs::msg::DiagnosticArray diag;
      if (decoder_->decode_diagnostics(frame, diag, now()) && pub_diag_->is_activated())
        pub_diag_->publish(diag);
      break;
    }

    case 0x310: {  // STEER_DIAG — v0.0.4 EPS-C telemetry
      if (frame.len < 8) break;
      uint16_t angle_raw = (uint16_t)frame.data[0] << 8 | frame.data[1];
      float steer_deg = (angle_raw - 30000) * 0.1f;  // offset=-3000, 0.1°/bit
      steer_angle_rad_ = steer_deg * M_PI / 180.0f;  // cache for odometry
      last_steer_time_ = now();
      autoware_auto_vehicle_msgs::msg::SteeringReport steer;
      steer.steering_tire_angle = steer_angle_rad_;
      if (pub_steering_->is_activated()) pub_steering_->publish(steer);
      break;
    }

    case 0x311: {  // BRAKE_DIAG — v0.0.4 SEB telemetry
      if (frame.len < 8) break;
      // Publish brake telemetry to diagnostics
      diagnostic_msgs::msg::DiagnosticArray diag;
      diag.header.stamp = now();
      auto add_kv = [&](const std::string & name, double val, const std::string & unit) {
        diagnostic_msgs::msg::DiagnosticStatus s;
        s.name = "brake/" + name;
        s.hardware_id = "etrike";
        s.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
        s.values = {{"value", std::to_string(val)}, {"unit", unit}};
        diag.status.push_back(s);
      };
      uint16_t press_raw = (uint16_t)((uint16_t)frame.data[0] << 8 | frame.data[1]);
      add_kv("pressure", press_raw * 0.05, "MPa");
      add_kv("fault", static_cast<double>(frame.data[2]), "bool");
      int16_t mtr_curr = (int16_t)((uint16_t)frame.data[3] << 8 | frame.data[4]);
      add_kv("motor_current", mtr_curr * 0.01, "A");
      uint16_t ecu_temp = (uint16_t)((uint16_t)frame.data[5] << 8 | frame.data[6]);
      add_kv("ecu_temp", ecu_temp * 0.1, "degC");
      if (pub_diag_->is_activated()) pub_diag_->publish(diag);
      break;
    }

    case CAN_RT_HB:
      rt_heartbeat_.feed(frame.data[0], now());
      break;

    default:
      break;
  }
}

}  // namespace autoware_vehicle_bridge

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<autoware_vehicle_bridge::VehicleBridgeNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node->get_node_base_interface());
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
