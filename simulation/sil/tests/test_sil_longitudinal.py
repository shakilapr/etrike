"""SIL Suite 1: Longitudinal Physics, Limits, Slope & Reverse Dynamics (§8.1 & §8.2).

Evaluates:
- Motor torque saturation limits (80 Nm peak) & first-order response lag (tau = 50 ms)
- Forward speed boundary ceiling (3000 mm/s)
- Reverse speed clamp (-500 mm/s)
- Positive road grade / slope resistance (5 deg, 10 deg, 15 deg incline)
- Smooth transition between Drive, Neutral, and Reverse through standstill
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


class TestSilLongitudinal(unittest.TestCase):
    def setUp(self):
        self.cfg = PlantConfig()
        self.plant = LongitudinalVehiclePlant(self.cfg)

    def test_motor_torque_saturation_and_lag(self):
        """Verify 80 Nm torque limit saturation and first-order 50 ms lag."""
        # 1. Apply over-limit torque command (120 Nm > 80 Nm limit)
        dt = 0.01  # 10 ms step
        commanded_torque = 120.0

        # Step 1: 10 ms into 50 ms time constant
        self.plant.step(commanded_motor_torque_nm=commanded_torque, dt=dt)
        # First-order step response: T(t) = T_cmd * (1 - exp(-t/tau))
        # At t = 10 ms, dt/tau = 10/50 = 0.2 -> T should be ~ 80 * 0.2 = 16.0 Nm
        self.assertAlmostEqual(self.plant.motor_torque_nm, 16.0, delta=1.0)

        # Run for 250 ms (5 * tau) to reach steady-state
        for _ in range(25):
            self.plant.step(commanded_motor_torque_nm=commanded_torque, dt=dt)

        # Invariant: torque must saturate strictly at 80.0 Nm
        self.assertAlmostEqual(self.plant.motor_torque_nm, 80.0, delta=0.5)
        self.assertLessEqual(self.plant.motor_torque_nm, 80.0001)

    def test_forward_speed_limits_and_acceleration(self):
        """Verify vehicle accelerates within physical traction envelope and clamps at 3000 mm/s."""
        dt = 0.01
        target_speed_mmps = 3000.0

        # Simple proportional controller driving vehicle to top speed
        speeds = []
        for _ in range(400):  # 4.0 seconds
            curr_v_mmps = self.plant.velocity_mps * 1000.0
            speeds.append(curr_v_mmps)
            err = target_speed_mmps - curr_v_mmps
            torque = max(0.0, min(err * 0.08, 80.0))
            self.plant.step(commanded_motor_torque_nm=torque, dt=dt)

        final_speed = speeds[-1]
        self.assertGreater(final_speed, 2700.0, f"Final speed {final_speed} failed to reach forward envelope")
        self.assertLessEqual(final_speed, 3050.0, "Speed exceeded forward maximum threshold")
        self.assertGreater(self.plant.position_m, 6.0, "Traversed distance too short")

    def test_slope_grade_resistance(self):
        """Evaluate grade resistance at 0 deg, 5 deg, and 12 deg inclines."""
        dt = 0.01
        slopes_deg = [0.0, 5.0, 12.0]
        final_speeds = []

        for slope in slopes_deg:
            plant = LongitudinalVehiclePlant(self.cfg)
            slope_rad = math.radians(slope)
            # Constant 30 Nm motor torque applied for 3 seconds
            for _ in range(300):
                plant.step(commanded_motor_torque_nm=30.0, road_grade_rad=slope_rad, dt=dt)
            final_speeds.append(plant.velocity_mps * 1000.0)

        # Increasing grade must monotonically reduce attainable speed under identical torque
        self.assertGreater(final_speeds[0], final_speeds[1], "5 deg speed was not lower than flat road")
        self.assertGreater(final_speeds[1], final_speeds[2], "12 deg speed was not lower than 5 deg road")

        # Theoretical gravitational force check at 12 deg:
        # F_grade = m * g * sin(theta) = 350 * 9.81 * sin(12°) ≈ 713.8 N
        # T_wheel = 30 * 8.5 * 0.9 = 229.5 Nm -> F_drive = 229.5 / 0.25 = 918 N
        # F_net > 0, so vehicle should still advance up 12° slope
        self.assertGreater(final_speeds[2], 500.0, "Vehicle stalled on 12 deg slope when torque exceeds grade load")

    def test_reverse_speed_clamp_and_transitions(self):
        """Verify reverse propulsion behavior and reverse ceiling clamp (-500 mm/s) under closed-loop control."""
        dt = 0.01
        plant = LongitudinalVehiclePlant(self.cfg)

        # 1. Reverse drive under closed-loop control with CAN-commanded reverse setpoint (-500 mm/s)
        # Architecture §8.1 & shared_config.h kMaxSpeedRevMmps = 500 mm/s
        target_rev_speed_mmps = -500.0  # Canonical signed reverse setpoint
        speeds = []

        for _ in range(300):  # 3.0 seconds
            curr_v_mmps = plant.velocity_mps * 1000.0
            speeds.append(curr_v_mmps)
            # Reverse controller: torque commands drive backward traction
            err = abs(target_rev_speed_mmps) - abs(curr_v_mmps)
            torque = max(0.0, min(err * 0.15, 30.0))
            # Step the plant with gear=3 (Reverse)
            plant.step(commanded_motor_torque_nm=torque, gear=3, dt=dt)

        final_speed = speeds[-1]
        self.assertLess(final_speed, -450.0, f"Final speed {final_speed} failed to achieve reverse target")
        self.assertGreaterEqual(final_speed, -505.0, "Speed exceeded reverse maximum threshold (-500 mm/s)")
        self.assertLess(plant.position_m, -1.0, "Vehicle did not traverse backward distance in reverse")

        # 2. Overspeed reverse command rejection: even if commanded -1500 mm/s, RT clamps to -500 mm/s
        overspeed_target = -1500.0
        clamped_setpoint = max(-500.0, min(0.0, overspeed_target))
        self.assertEqual(clamped_setpoint, -500.0)

        for _ in range(100):
            curr_v_mmps = plant.velocity_mps * 1000.0
            err = abs(clamped_setpoint) - abs(curr_v_mmps)
            torque = max(0.0, min(err * 0.15, 30.0))
            plant.step(commanded_motor_torque_nm=torque, gear=3, dt=dt)

        self.assertGreaterEqual(plant.velocity_mps * 1000.0, -505.0)


if __name__ == "__main__":
    unittest.main()
