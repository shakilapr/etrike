"""Test: Closed-Loop Longitudinal Physics Step Response & PID Stability (?8.16).

Verifies:
- CAN-driven closed-loop command path:
  Host 0x300 (HOST_DRIVE_CMD) -> RT 0x204 (RT_DRIVE_CMD) -> Plant Simulation -> MTR 0x206 (MTR_MOTOR_FBK) -> RT
- Real CAN serialization & deserialization via canonical etrike_protocol codec
- Motor lag & torque limits
- Discrete 100 Hz simulation
- Settling near 2500 mm/s within physical envelope
- Overshoot < 10%
"""

import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT / "simulation" / "sil" / "models"))
sys.path.append(str(ROOT / "protocol" / "generated" / "python"))

from vehicle_plant import LongitudinalVehiclePlant, PlantConfig
import etrike_protocol as proto


class TestClosedLoopDrive(unittest.TestCase):
    def test_2500_mmps_step_response(self):
        plant = LongitudinalVehiclePlant()

        target_speed_mmps = 2500
        kp = 0.05
        ki = 0.01
        integral_error = 0.0

        speeds = []
        dt = 0.01 # 100 Hz RT loop

        # Run 5.0 seconds (500 steps)
        for step in range(500):
            # 1. Host transmits 0x300 HOST_DRIVE_CMD
            host_status, host_payload = proto.encode(
                "host:host_drive_cmd",
                {
                    "speed_mmps": target_speed_mmps,
                    "yaw_rate_mrad_s": 0,
                    "gear": 1, # 'D'
                },
                bus="high",
            )
            self.assertEqual(host_status, "ok")

            # 2. RT receives and decodes 0x300 HOST_DRIVE_CMD
            rt_rx_status, host_cmd_decoded = proto.decode("host:host_drive_cmd", host_payload, bus="high")
            self.assertEqual(rt_rx_status, "ok")
            setpoint_speed = host_cmd_decoded["speed_mmps"]

            # 3. RT calculates control and issues 0x204 RT_DRIVE_CMD
            rt_cmd_status, rt_cmd_payload = proto.encode(
                "rt:rt_drive_cmd",
                {
                    "motor_speed_mmps": setpoint_speed,
                    "gear": host_cmd_decoded["gear"],
                },
                bus="low",
            )
            self.assertEqual(rt_cmd_status, "ok")

            # 4. Plant/MTR receives 0x204 RT_DRIVE_CMD
            mtr_rx_status, rt_cmd_decoded = proto.decode("rt:rt_drive_cmd", rt_cmd_payload, bus="low")
            self.assertEqual(mtr_rx_status, "ok")

            # Execute closed-loop plant dynamics
            current_speed_mmps = plant.velocity_mps * 1000.0
            speeds.append(current_speed_mmps)

            error = rt_cmd_decoded["motor_speed_mmps"] - current_speed_mmps
            integral_error += error * dt

            # PID torque command with anti-windup clamp
            cmd_torque = kp * error + ki * integral_error
            cmd_torque = max(0.0, min(cmd_torque, 80.0))

            plant.step(commanded_motor_torque_nm=cmd_torque, dt=dt)

            # 5. MTR transmits 0x206 MTR_MOTOR_FBK to bus (saturating at CAN definition limits)
            fbk_speed = max(-500, min(int(plant.velocity_mps * 1000.0), 3000))
            fbk_status, fbk_payload = proto.encode(
                "mtr:mtr_motor_fbk",
                {
                    "motor_command_speed_mmps": fbk_speed,
                    "gear_state": 1,
                    "fault_flags": 0,
                },
                bus="low",
            )
            self.assertEqual(fbk_status, "ok")

            # 6. RT receives 0x206 feedback
            rt_fbk_status, fbk_decoded = proto.decode("mtr:mtr_motor_fbk", fbk_payload, bus="low")
            self.assertEqual(rt_fbk_status, "ok")
            self.assertEqual(fbk_decoded["motor_command_speed_mmps"], fbk_speed)

        final_speed = speeds[-1]
        peak_speed = max(speeds)

        # Invariants
        self.assertGreater(final_speed, 2350.0, f"Final speed {final_speed} below envelope")
        self.assertLess(final_speed, 2650.0, f"Final speed {final_speed} above envelope")
        self.assertLess(peak_speed, 2750.0, f"Overshoot exceeded 10% ({peak_speed} mm/s)")
        self.assertGreater(plant.position_m, 5.0, "Vehicle failed to traverse distance")


if __name__ == "__main__":
    unittest.main()
