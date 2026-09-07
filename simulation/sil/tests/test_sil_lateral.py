"""SIL Suite 2: Lateral Kinematics, Steering, Yaw Rate & Stability Limits (§8.8 & §8.9).

Evaluates:
- Delta tricycle inverse bicycle model: delta = atan(L * w / v), wheelbase L = 1500 mm
- Physical steering hard lock clamps (+-40 deg) & saturation detection
- Standstill zero-speed yaw protection (front wheel aligns without producing forward lurch)
- Dynamic speed-dependent angle clamp: delta_max(v) = 40.0 - (v_kmh - 2.0) * (35.0/23.0), clamped [5.0, 40.0]
- Lateral acceleration a_y = v^2 / L * tan(delta) vs static rollover margin (w = 800 mm, h = 500 mm)
"""

import math
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT))
sys.path.append(str(ROOT / "simulation" / "sil" / "models"))

WHEELBASE_M = 1.500     # 1500 mm
TRACK_WIDTH_M = 0.800   # 800 mm
CG_HEIGHT_M = 0.500     # 500 mm
STEER_HARD_LIMIT_DEG = 40.0
GRAVITY_MPS2 = 9.81


def compute_dynamic_limit_deg(speed_mmps: float) -> float:
    speed_kmh = abs(speed_mmps) * 3.6 / 1000.0
    limit_deg = 40.0 - (speed_kmh - 2.0) * (35.0 / 23.0)
    return max(5.0, min(40.0, limit_deg))


def resolve_tricycle_kinematics(v_mmps: float, yaw_rate_mrad_s: float):
    v = v_mmps / 1000.0
    w = yaw_rate_mrad_s / 1000.0
    L = WHEELBASE_M
    low_speed_mps = 0.050

    steer_limit_rad = math.radians(STEER_HARD_LIMIT_DEG)

    if abs(v) > low_speed_mps:
        requested_steer = math.atan((L * w) / v)
        saturated = abs(requested_steer) > steer_limit_rad
        steer = max(-steer_limit_rad, min(steer_limit_rad, requested_steer))
        return {
            "motor_speed_mmps": int(v * 1000.0),
            "steer_angle_deg": math.degrees(steer),
            "steer_valid": not saturated,
            "steer_saturated": saturated,
            "reversing": v < 0,
        }
    elif abs(w) > 0.001:
        # Standstill with yaw: align wheel to full lock, keep speed 0
        steer = steer_limit_rad if w > 0 else -steer_limit_rad
        return {
            "motor_speed_mmps": 0,
            "steer_angle_deg": math.degrees(steer),
            "steer_valid": True,
            "steer_saturated": False,
            "reversing": False,
        }
    else:
        return {
            "motor_speed_mmps": 0,
            "steer_angle_deg": 0.0,
            "steer_valid": True,
            "steer_saturated": False,
            "reversing": False,
        }


class TestSilLateral(unittest.TestCase):
    def test_inverse_bicycle_kinematics_accuracy(self):
        """Verify atan(L * w / v) calculation against known trigonometric vectors."""
        # 1. Forward 1.5 m/s (1500 mm/s), yaw rate 0.5 rad/s (500 mrad/s)
        # delta = atan(1.5 * 0.5 / 1.5) = atan(0.5) = 26.565 deg
        res = resolve_tricycle_kinematics(1500.0, 500.0)
        self.assertAlmostEqual(res["steer_angle_deg"], 26.565, places=2)
        self.assertTrue(res["steer_valid"])
        self.assertFalse(res["steer_saturated"])

        # 2. Forward 2.0 m/s, yaw rate 0.8 rad/s
        # delta = atan(1.5 * 0.8 / 2.0) = atan(0.6) = 30.963 deg
        res2 = resolve_tricycle_kinematics(2000.0, 800.0)
        self.assertAlmostEqual(res2["steer_angle_deg"], 30.963, places=2)
        self.assertTrue(res2["steer_valid"])
        self.assertFalse(res2["steer_saturated"])

    def test_steering_hard_clamp_and_saturation(self):
        """Verify +-40 deg physical hard lock and saturation flag."""
        # High yaw rate requesting steep angle: w = 2.0 rad/s at v = 1.0 m/s
        # requested = atan(1.5 * 2.0 / 1.0) = atan(3.0) = 71.56 deg > 40 deg
        res = resolve_tricycle_kinematics(1000.0, 2000.0)
        self.assertAlmostEqual(res["steer_angle_deg"], 40.0, places=3)
        self.assertTrue(res["steer_saturated"])
        self.assertFalse(res["steer_valid"])

        # Negative direction
        res_neg = resolve_tricycle_kinematics(1000.0, -2000.0)
        self.assertAlmostEqual(res_neg["steer_angle_deg"], -40.0, places=3)
        self.assertTrue(res_neg["steer_saturated"])
        self.assertFalse(res_neg["steer_valid"])

    def test_standstill_zero_speed_yaw_protection(self):
        """Verify bug 4.5 fix: yaw command at standstill aligns wheels but never generates forward speed."""
        res = resolve_tricycle_kinematics(0.0, 500.0)
        self.assertEqual(res["motor_speed_mmps"], 0, "Forward speed was erroneously generated at standstill!")
        self.assertAlmostEqual(res["steer_angle_deg"], 40.0, places=3)
        self.assertTrue(res["steer_valid"])

        res_left = resolve_tricycle_kinematics(0.0, -500.0)
        self.assertEqual(res_left["motor_speed_mmps"], 0)
        self.assertAlmostEqual(res_left["steer_angle_deg"], -40.0, places=3)

    def test_dynamic_speed_clamping_and_rollover_envelope(self):
        """Verify speed-dependent steering reduction prevents lateral rollover."""
        # Speed: 25.0 km/h (6944.4 mm/s) -> clamp should be 5.0 deg
        limit_high = compute_dynamic_limit_deg(25.0 * 1000.0 / 3.6)
        self.assertAlmostEqual(limit_high, 5.0, places=3, msg="Dynamic limit did not clamp to 5 deg at top speed")

        # Speed: 2.0 km/h (555 mm/s) -> clamp should be 40.0 deg
        limit_low = compute_dynamic_limit_deg(555.0)
        self.assertEqual(limit_low, 40.0, "Dynamic limit was restricted at parking speeds")

        # Intermediate speeds: verify monotonic decrease
        speeds_kmh = [2.0, 5.0, 10.0, 15.0, 20.0, 25.0]
        limits = [compute_dynamic_limit_deg(s * 1000.0 / 3.6) for s in speeds_kmh]
        for i in range(len(limits) - 1):
            self.assertGreaterEqual(limits[i], limits[i+1], "Limits must monotonically decrease with speed")

        # Critical rollover boundary check:
        # Rollover threshold: a_y < g * w / (2 * h)
        # a_y_max = 9.81 * 0.8 / (2 * 0.5) = 7.848 m/s^2 (~0.8g)
        max_allowable_ay = GRAVITY_MPS2 * TRACK_WIDTH_M / (2.0 * CG_HEIGHT_M)

        # For every legal speed up to 25 km/h with its dynamic steering clamp:
        for s_kmh in speeds_kmh:
            v_mps = s_kmh / 3.6
            max_deg = compute_dynamic_limit_deg(v_mps * 1000.0)
            steer_rad = math.radians(max_deg)
            # Lateral acceleration: a_y = v^2 / L * tan(delta)
            ay = (v_mps ** 2) / WHEELBASE_M * math.tan(steer_rad)
            # Verify dynamic clamp strictly maintains safe rollover margin
            self.assertLess(ay, max_allowable_ay, f"Rollover margin breached at {s_kmh} km/h (a_y={ay:.2f} m/s^2)")


if __name__ == "__main__":
    unittest.main()
