// Copyright 2026 E-Trike
// Licensed under the Apache License, Version 2.0

#ifndef AUTOWARE_VEHICLE_BRIDGE__VEHICLE_BRIDGE_NODE_HPP_
#define AUTOWARE_VEHICLE_BRIDGE__VEHICLE_BRIDGE_NODE_HPP_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp_lifecycle/lifecycle_publisher.hpp>

#include <autoware_control_msgs/msg/control.hpp>
#include <autoware_vehicle_msgs/msg/control_mode_report.hpp>
#include <autoware_vehicle_msgs/msg/engage.hpp>
#include <autoware_vehicle_msgs/msg/gear_command.hpp>
#include <autoware_vehicle_msgs/msg/gear_report.hpp>
#include <autoware_vehicle_msgs/msg/hazard_lights_command.hpp>
#include <autoware_vehicle_msgs/msg/hazard_lights_report.hpp>
#include <autoware_vehicle_msgs/msg/steering_report.hpp>
#include <autoware_vehicle_msgs/msg/turn_indicators_command.hpp>
#include <autoware_vehicle_msgs/msg/turn_indicators_report.hpp>
#include <autoware_vehicle_msgs/msg/velocity_report.hpp>
#include <autoware_vehicle_msgs/srv/control_mode_command.hpp>
#include <tier4_vehicle_msgs/msg/vehicle_emergency_stamped.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>

struct can_frame;

namespace autoware_vehicle_bridge
{

// ---- CAN driver abstraction (testable, replaceable) ----
class CanDriver
{
public:
  virtual ~CanDriver() = default;
  virtual bool open(const std::string & interface) = 0;
  virtual void close() = 0;
  virtual bool send(const struct can_frame & frame) = 0;
  virtual bool receive(struct can_frame & frame, int timeout_ms) = 0;
  virtual bool is_open() const = 0;
};

class SocketCanDriver : public CanDriver
{
public:
  bool open(const std::string & interface) override;
  void close() override;
  bool send(const struct can_frame & frame) override;
  bool receive(struct can_frame & frame, int timeout_ms) override;
  bool is_open() const override { return fd_ >= 0; }
  int fd() const { return fd_; }

private:
  int fd_{-1};
};

// ---- Vehicle parameters (immutable after configure) ----
struct VehicleParams
{
  double wheel_base{1.5};
  double max_speed_forward{3.0};
  double max_speed_reverse{0.5};
  double max_steering_angle{0.698};
  double max_brake_pressure_kpa{5000.0};
  double max_deceleration{5.0};
  double low_speed_threshold{0.05};
  double loop_rate{100.0};
  int command_timeout_ms{500};
  int heartbeat_interval_ms{500};
  int rt_heartbeat_timeout_ms{1500};
  std::string can_interface{"can0"};

  bool load_from(const rclcpp::Node * node);
  void validate_or_throw() const;
};

// ---- CAN encoder: ROS message -> CAN frame ----
class CanEncoder
{
public:
  explicit CanEncoder(const VehicleParams & params);

  // Returns true if a frame should be sent
  bool encode_drive(const autoware_control_msgs::msg::Control & cmd,
                    uint8_t gear_override, bool has_gear_override,
                    struct can_frame & frame);

  bool encode_brake(const autoware_control_msgs::msg::Control & cmd,
                    struct can_frame & frame);

  bool encode_lights(const autoware_vehicle_msgs::msg::TurnIndicatorsCommand * turn,
                     const autoware_vehicle_msgs::msg::HazardLightsCommand * hazard,
                     bool is_braking,
                     struct can_frame & frame);

  bool encode_heartbeat(struct can_frame & frame);
  bool encode_estop(struct can_frame & frame);
  bool encode_mode_request(bool autonomous, struct can_frame & frame);

private:
  const VehicleParams & params_;
  uint8_t host_alive_ctr_{0};
  uint8_t mode_request_ctr_{0};

  int32_t speed_to_mmps(float speed_ms) const;
  int32_t steering_to_yaw(float angle_rad, float speed_ms) const;
  uint8_t derive_gear(int32_t speed_mmps, uint8_t gear_override, bool has_override) const;
};

// ---- CAN decoder: CAN frame -> ROS message ----
class CanDecoder
{
public:
  bool decode_velocity(const struct can_frame & frame,
                       autoware_vehicle_msgs::msg::VelocityReport & msg) const;

  bool decode_state(const struct can_frame & frame,
                    autoware_vehicle_msgs::msg::ControlModeReport & mode_msg,
                    autoware_vehicle_msgs::msg::GearReport & gear_msg) const;

  bool decode_diagnostics(const struct can_frame & frame,
                          diagnostic_msgs::msg::DiagnosticArray & msg,
                          const rclcpp::Time & now) const;
};

// ---- Heartbeat monitor (thread-safe: accessed from RX and executor threads) ----
class HeartbeatMonitor
{
public:
  void feed(uint8_t counter, const rclcpp::Time & now);
  bool is_alive(const rclcpp::Time & now, int timeout_ms) const;
  uint8_t counter() const { std::lock_guard<std::mutex> lk(mutex_); return counter_; }
  rclcpp::Time last_time() const { std::lock_guard<std::mutex> lk(mutex_); return last_time_; }

private:
  mutable std::mutex mutex_;
  uint8_t counter_{0};
  rclcpp::Time last_time_{0, 0, RCL_SYSTEM_TIME};
};

// ---- Main node ----
class VehicleBridgeNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  explicit VehicleBridgeNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~VehicleBridgeNode() override;

  CallbackReturn on_configure(const State & previous_state) override;
  CallbackReturn on_activate(const State & previous_state) override;
  CallbackReturn on_deactivate(const State & previous_state) override;
  CallbackReturn on_cleanup(const State & previous_state) override;
  CallbackReturn on_shutdown(const State & previous_state) override;

  // Testing hooks
  void set_can_driver(std::unique_ptr<CanDriver> driver);

private:
  // ---- Dependencies ----
  std::unique_ptr<CanDriver> can_;
  VehicleParams params_;
  std::unique_ptr<CanEncoder> encoder_;
  std::unique_ptr<CanDecoder> decoder_;

  // ---- Subscriptions ----
  rclcpp::Subscription<autoware_control_msgs::msg::Control>::SharedPtr sub_control_;
  rclcpp::Subscription<autoware_vehicle_msgs::msg::GearCommand>::SharedPtr sub_gear_;
  rclcpp::Subscription<autoware_vehicle_msgs::msg::TurnIndicatorsCommand>::SharedPtr sub_turn_;
  rclcpp::Subscription<autoware_vehicle_msgs::msg::HazardLightsCommand>::SharedPtr sub_hazard_;
  rclcpp::Subscription<autoware_vehicle_msgs::msg::Engage>::SharedPtr sub_engage_;
  rclcpp::Subscription<tier4_vehicle_msgs::msg::VehicleEmergencyStamped>::SharedPtr sub_emergency_;
  rclcpp::Service<autoware_vehicle_msgs::srv::ControlModeCommand>::SharedPtr srv_control_mode_;

  // ---- Publishers ----
  rclcpp_lifecycle::LifecyclePublisher<autoware_vehicle_msgs::msg::VelocityReport>::SharedPtr pub_velocity_;
  rclcpp_lifecycle::LifecyclePublisher<autoware_vehicle_msgs::msg::SteeringReport>::SharedPtr pub_steering_;
  rclcpp_lifecycle::LifecyclePublisher<autoware_vehicle_msgs::msg::GearReport>::SharedPtr pub_gear_;
  rclcpp_lifecycle::LifecyclePublisher<autoware_vehicle_msgs::msg::ControlModeReport>::SharedPtr pub_mode_;
  rclcpp_lifecycle::LifecyclePublisher<autoware_vehicle_msgs::msg::TurnIndicatorsReport>::SharedPtr pub_turn_status_;
  rclcpp_lifecycle::LifecyclePublisher<autoware_vehicle_msgs::msg::HazardLightsReport>::SharedPtr pub_hazard_status_;
  rclcpp_lifecycle::LifecyclePublisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr pub_diag_;

  // ---- Timers ----
  rclcpp::TimerBase::SharedPtr timer_control_;
  rclcpp::TimerBase::SharedPtr timer_heartbeat_;
  rclcpp::TimerBase::SharedPtr timer_diag_;

  // ---- CAN receive thread ----
  std::thread rx_thread_;
  std::atomic<bool> rx_running_{false};

  // ---- Command state (mutex-protected) ----
  std::mutex mutex_;
  autoware_control_msgs::msg::Control::SharedPtr latest_control_;
  autoware_vehicle_msgs::msg::GearCommand::SharedPtr latest_gear_;
  autoware_vehicle_msgs::msg::TurnIndicatorsCommand::SharedPtr latest_turn_;
  autoware_vehicle_msgs::msg::HazardLightsCommand::SharedPtr latest_hazard_;
  rclcpp::Time last_cmd_time_;
  std::atomic<bool> engaged_{false};
  std::atomic<bool> confirmed_auto_{false};
  std::atomic<bool> software_emergency_{false};

  // ---- Heartbeat ----
  HeartbeatMonitor rt_heartbeat_;

  // ---- SYS liveness (from 0x011, written in RX thread, read in executor thread) ----
  std::atomic<uint8_t> sys_estop_active_{0};
  std::atomic<uint8_t> sys_heartbeat_ok_{1};

  // ---- Rate limiting ----
  rclcpp::Time last_estop_tx_{0, 0, RCL_SYSTEM_TIME};

  // ---- Callbacks ----
  void on_control(const autoware_control_msgs::msg::Control::SharedPtr msg);
  void on_gear(const autoware_vehicle_msgs::msg::GearCommand::SharedPtr msg);
  void on_turn(const autoware_vehicle_msgs::msg::TurnIndicatorsCommand::SharedPtr msg);
  void on_hazard(const autoware_vehicle_msgs::msg::HazardLightsCommand::SharedPtr msg);
  void on_engage(const autoware_vehicle_msgs::msg::Engage::SharedPtr msg);
  void on_control_mode(
    const std::shared_ptr<autoware_vehicle_msgs::srv::ControlModeCommand::Request> request,
    std::shared_ptr<autoware_vehicle_msgs::srv::ControlModeCommand::Response> response);
  void on_emergency(const tier4_vehicle_msgs::msg::VehicleEmergencyStamped::SharedPtr msg);

  void tick_control();
  void tick_heartbeat();
  void tick_diagnostics();
  void run_can_receive();

  // ---- Helpers ----
  bool load_parameters();
  void publish_vehicle_reports(const struct can_frame & frame);
};

}  // namespace autoware_vehicle_bridge

#endif  // AUTOWARE_VEHICLE_BRIDGE__VEHICLE_BRIDGE_NODE_HPP_
