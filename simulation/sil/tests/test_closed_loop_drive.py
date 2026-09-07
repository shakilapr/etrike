"""Test: Closed-Loop Longitudinal Physics Step Response & PID Stability (§8.16).

Verifies:
- Command 3000 mm/s setpoint
- Motor lag & torque limits
- Discrete 100 Hz simulation
- Settling near 3000 mm/s within physical envelope
- Overshoot < 10%
"""

import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT / "simulation" / "sil" / "models"))

from vehicle_plant import LongitudinalVehiclePlant, PlantConfig


class TestClosedLoopDrive(unittest.TestCase):
    def test_3000_mmps_step_response(self):
        plant = LongitudinalVehiclePlant()

        target_speed_mmps = 3000.0
        kp = 0.05
        ki = 0.01
        integral_error = 0.0

        speeds = []
        dt = 0.01 # 100 Hz RT loop

        # Run 5.0 seconds (500 steps)
        for step in range(500):
            current_speed_mmps = plant.velocity_mps * 1000.0
            speeds.append(current_speed_mmps)

            error = target_speed_mmps - current_speed_mmps
            integral_error += error * dt

            # PID torque command with anti-windup clamp
            cmd_torque = kp * error + ki * integral_error
            cmd_torque = max(0.0, min(cmd_torque, 80.0))

            plant.step(commanded_motor_torque_nm=cmd_torque, dt=dt)

        final_speed = speeds[-1]
        peak_speed = max(speeds)

        # Invariants
        self.assertGreater(final_speed, 2800.0, f"Final speed {final_speed} below envelope")
        self.assertLess(final_speed, 3150.0, f"Final speed {final_speed} above envelope")
        self.assertLess(peak_speed, 3300.0, f"Overshoot exceeded 10% ({peak_speed} mm/s)")
        self.assertGreater(plant.position_m, 5.0, "Vehicle failed to traverse distance")


if __name__ == "__main__":
    unittest.main()
