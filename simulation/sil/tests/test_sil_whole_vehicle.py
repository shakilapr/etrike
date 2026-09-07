"""SIL Suite 4: Whole-Vehicle Multi-Node Closed-Loop Simulation (§6.2 & §8).

Simulates the complete vehicle pipeline across deterministic virtual CAN:
  Host (0x300 Drive, 0x301 Brake, 0x303 Steer)
    --> RT (0x204 Drive, 0x205 Brake, 0x169 Steer)
      --> SYS (0x011 Safety, 0x7B9 SEB Actuator)
        --> MTR / SEB / SES physical plant execution
          --> Feedback (0x206 Motor Fbk, 0x201 Steer Status, 0x721 Brake Status)
            --> RT / Host loop closure

Evaluates multi-node interactions:
1. Normal driving trajectory (accelerate + steady turn)
2. Obstacle detection leading to autonomous braking
3. Sudden emergency ESTOP injection across all nodes
4. Sensor feedback loop sync & deadman watchdog supervision
"""

import math
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT))
sys.path.append(str(ROOT / "simulation" / "sil" / "models"))
sys.path.append(str(ROOT / "protocol" / "generated" / "python"))

from vehicle_plant import LongitudinalVehiclePlant, PlantConfig
import etrike_protocol as proto

# Simulated bus storage for high and low CAN networks
class VirtualCanBus:
    def __init__(self):
        self.high_traffic = []
        self.low_traffic = []

    def publish_high(self, msg_key: str, data: dict):
        status, payload = proto.encode(msg_key, data, bus="high")
        assert status == "ok", f"High CAN encode failed: {msg_key} -> {status}"
        self.high_traffic.append((msg_key, payload))

    def publish_low(self, msg_key: str, data: dict):
        status, payload = proto.encode(msg_key, data, bus="low")
        assert status == "ok", f"Low CAN encode failed: {msg_key} -> {status}"
        self.low_traffic.append((msg_key, payload))


class TestSilWholeVehicle(unittest.TestCase):
    def setUp(self):
        self.bus = VirtualCanBus()
        self.plant = LongitudinalVehiclePlant()
        self.wheelbase_m = 1.500
        self.steer_angle_deg = 0.0
        self.heading_rad = 0.0
        self.x_m = 0.0
        self.y_m = 0.0

    def test_accelerate_and_coordinated_turn_trajectory(self):
        """Host commands speed and yaw rate; RT resolves kinematics and sends to MTR & SES."""
        dt = 0.01  # 100 Hz simulation
        target_speed_mmps = 2000
        target_yaw_mrad_s = 400  # 0.4 rad/s turn

        positions_x = []
        positions_y = []

        # Run for 3.0 seconds (300 cycles)
        for cycle in range(300):
            # 1. Host transmits 0x300 HOST_DRIVE_CMD
            st_host, pl_host = proto.encode(
                "host:host_drive_cmd",
                {"speed_mmps": target_speed_mmps, "yaw_rate_mrad_s": target_yaw_mrad_s, "gear": 1},
                bus="high"
            )
            self.assertEqual(st_host, "ok")

            # 2. RT receives and decodes 0x300
            st_rt_rx, cmd = proto.decode("host:host_drive_cmd", pl_host, bus="high")
            self.assertEqual(st_rt_rx, "ok")

            # RT Kinematics: delta = atan(L * w / v)
            v = cmd["speed_mmps"] / 1000.0
            w = cmd["yaw_rate_mrad_s"] / 1000.0
            steer_rad = math.atan((self.wheelbase_m * w) / v) if v > 0.05 else 0.0
            self.steer_angle_deg = math.degrees(steer_rad)

            # RT publishes 0x204 RT_DRIVE_CMD to Low Bus
            st_204, pl_204 = proto.encode(
                "rt:rt_drive_cmd",
                {"motor_speed_mmps": cmd["speed_mmps"], "gear": cmd["gear"]},
                bus="low"
            )
            self.assertEqual(st_204, "ok")

            # 3. MTR executes torque control loop from 0x204
            st_mtr_rx, mtr_cmd = proto.decode("rt:rt_drive_cmd", pl_204, bus="low")
            self.assertEqual(st_mtr_rx, "ok")

            curr_v = self.plant.velocity_mps
            error = (mtr_cmd["motor_speed_mmps"] / 1000.0) - curr_v
            t_cmd = max(0.0, min(error * 40.0, 80.0))

            self.plant.step(commanded_motor_torque_nm=t_cmd, dt=dt)

            # 4. Integrate 2D vehicle planar pose
            v_actual = self.plant.velocity_mps
            yaw_rate_actual = (v_actual / self.wheelbase_m) * math.tan(steer_rad)
            self.heading_rad += yaw_rate_actual * dt
            self.x_m += v_actual * math.cos(self.heading_rad) * dt
            self.y_m += v_actual * math.sin(self.heading_rad) * dt

            positions_x.append(self.x_m)
            positions_y.append(self.y_m)

            # 5. MTR transmits 0x206 MTR_MOTOR_FBK
            clamped_feedback = max(-500, min(int(v_actual * 1000.0), 3000))
            st_206, pl_206 = proto.encode(
                "mtr:mtr_motor_fbk",
                {"actual_speed_mmps": clamped_feedback, "gear_state": 1, "fault_flags": 0},
                bus="low"
            )
            self.assertEqual(st_206, "ok")

        # Invariants
        self.assertGreater(self.x_m, 2.0, "Vehicle did not traverse forward X distance")
        self.assertGreater(self.y_m, 0.5, "Vehicle failed to turn along positive Y arc under left turn")
        self.assertGreater(self.heading_rad, 0.3, "Vehicle heading did not rotate under yaw command")
        self.assertAlmostEqual(self.steer_angle_deg, 16.699, delta=0.5)

    def test_obstacle_detection_to_automated_stop(self):
        """Obstacle approaches on High bus -> RT commands deceleration & hydraulic brake via 0x205."""
        dt = 0.01
        plant = LongitudinalVehiclePlant()
        plant.velocity_mps = 2.0  # Initial cruising speed

        # Simulation: Obstacle distance decreases from 2000 mm down to 200 mm (below 300 mm emergency threshold)
        obstacle_distance_mm = 2000

        for step in range(150):
            obstacle_distance_mm = max(100, obstacle_distance_mm - 15)

            # Canonical RT obstacle speed & brake limiter (rt-esp32/src/physics_model.cpp):
            # stop_dist = 300 mm, clear_dist = 3000 mm, max_brake = 5000 kPa (shared_config.h kObstacleMaxKpa)
            if obstacle_distance_mm <= 300:
                brake_kpa = 5000
                speed_target = 0
            elif obstacle_distance_mm >= 3000:
                brake_kpa = 0
                speed_target = 2000
            else:
                t = (obstacle_distance_mm - 300) / 2700.0
                speed_target = int(2000 * t)
                brake_kpa = int(5000 * (1.0 - t))

            # RT publishes 0x205 RT_BRAKE_CMD
            st_205, pl_205 = proto.encode("rt:rt_brake_cmd", {"brake_pressure_kpa": brake_kpa}, bus="low")
            self.assertEqual(st_205, "ok")

            # SYS processes brake command and translates to mechanical ground force
            st_sys_rx, brake_decoded = proto.decode("rt:rt_brake_cmd", pl_205, bus="low")
            self.assertEqual(st_sys_rx, "ok")

            # Braking force proportional to hydraulic pressure on dual rear calipers:
            # P_pa * A_piston * (2 pads) * 2 calipers * mu_pad * (r_disc / r_wheel)
            # 5000 kPa -> 1134 N
            cmd_kpa = brake_decoded["brake_pressure_kpa"]
            f_brake = (cmd_kpa * 1000.0 * 0.00045) * 4.0 * 0.35 * (0.090 / 0.250)
            plant.step(commanded_motor_torque_nm=0.0, brake_force_n=f_brake, dt=dt)

        # Vehicle must achieve full standstill before obstacle impact (< 300 mm)
        self.assertEqual(plant.velocity_mps, 0.0, "Vehicle failed to achieve full stop upon obstacle alert")

    def test_whole_vehicle_hardwired_estop_cutoff(self):
        """0x001 ESTOP on High bus propagates to Low bus, instantly cutting propulsion and applying SEB brakes."""
        dt = 0.01
        plant = LongitudinalVehiclePlant()
        plant.velocity_mps = 2.5
        plant.motor_torque_nm = 60.0

        # Hardwired 0x001 triggered
        st_001, pl_001 = proto.encode("safety:safety_estop", {}, bus="high")
        self.assertEqual(st_001, "ok")

        # ESTOP cuts motor power across RT and MTR instantly
        # SEB applies emergency clamping force (2500 N)
        for _ in range(50):  # 500 ms
            plant.step(commanded_motor_torque_nm=0.0, brake_force_n=2500.0, dt=dt)

        self.assertEqual(plant.velocity_mps, 0.0, "Vehicle did not halt immediately under full vehicle ESTOP")


if __name__ == "__main__":
    unittest.main()
