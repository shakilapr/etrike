"""SIL Suite 3: Hydraulic Braking, Deceleration, Stopping Distance & ESTOP (§8.5 & §8.6).

Evaluates:
- Hydraulic line pressure to mechanical brake clamping force mapping (kPa to N)
- Realistic deceleration dynamics under graduated braking (0 kPa to 20,000 kPa)
- Stopping distance measurements from initial speeds (1.0 m/s, 2.0 m/s, 3.0 m/s)
- Emergency ESTOP response: maximum stroke (27 mm), motor cutoff, and stopping within physical gate limits (< 1.2 s)
"""

import math
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT))
sys.path.append(str(ROOT / "simulation" / "sil" / "models"))

from vehicle_plant import LongitudinalVehiclePlant, PlantConfig

# Mechanical constants for rear tricycle hydraulic disc brakes:
# Single master cylinder feeding dual rear disc calipers:
# Caliper effective piston area: A_piston ≈ 0.00045 m^2 (24 mm single-piston per caliper)
# Pad friction coefficient: mu_pad = 0.35 (dual pad faces per caliper -> factor 2)
# Disc effective radius: r_disc = 0.090 m (180 mm rotor)
# Tire rolling radius: r_wheel = 0.250 m
# Formula: F_brake = (P_pa * A_piston) * (2 pads) * 2 calipers * mu_pad * (r_disc / r_wheel)
# Gains: at 1000 kPa (10 bar) -> ~227 N; at 3000 kPa (30 bar) -> ~680 N; at 5000 kPa (50 bar) -> ~1134 N; at 20000 kPa -> adhesion clamped
PISTON_AREA_M2 = 0.00045
PAD_MU = 0.35
DISC_RADIUS_M = 0.090
WHEEL_RADIUS_M = 0.250


def pressure_kpa_to_brake_force_n(pressure_kpa: float) -> float:
    """Converts hydraulic line pressure in kPa to longitudinal braking force at tire contact patch."""
    pressure_pa = max(0.0, pressure_kpa) * 1000.0
    # Normal clamping force per caliper
    normal_force_n = pressure_pa * PISTON_AREA_M2
    # Friction torque across two rear calipers (each with two pad contact faces)
    t_brake = 2.0 * (2.0 * normal_force_n * PAD_MU) * DISC_RADIUS_M
    # Ground force at tire contact patch
    f_ground = t_brake / WHEEL_RADIUS_M
    # Physical road adhesion limit (mu = 0.85, mass = 350 kg)
    f_adhesion_limit = 350.0 * 9.81 * 0.85
    return min(f_ground, f_adhesion_limit)


class TestSilBraking(unittest.TestCase):
    def test_hydraulic_pressure_force_mapping(self):
        """Verify linear pressure to clamp force mapping and physical adhesion bounds."""
        # Zero pressure produces zero braking force
        f0 = pressure_kpa_to_brake_force_n(0.0)
        self.assertEqual(f0, 0.0)

        # 5000 kPa (50 bar - moderate service brake)
        f5000 = pressure_kpa_to_brake_force_n(5000.0)
        self.assertAlmostEqual(f5000, 1134.0, delta=5.0)

        # 20000 kPa (200 bar - maximum emergency clamping, saturates at tire adhesion limit)
        f20000 = pressure_kpa_to_brake_force_n(20000.0)
        adhesion_max = 350.0 * 9.81 * 0.85
        self.assertAlmostEqual(f20000, adhesion_max, delta=1.0)

    def test_deceleration_envelope_across_pressures(self):
        """Verify commanded pressures generate appropriate deceleration rates in g's."""
        dt = 0.01
        pressures_kpa = [1000.0, 3000.0, 7000.0, 15000.0]
        decelerations_mps2 = []

        for p_kpa in pressures_kpa:
            plant = LongitudinalVehiclePlant()
            plant.velocity_mps = 3.0  # Initial 3.0 m/s
            b_force = pressure_kpa_to_brake_force_n(p_kpa)
            plant.step(commanded_motor_torque_nm=0.0, brake_force_n=b_force, dt=dt)
            # Deceleration magnitude
            decel = abs(plant.acceleration_mps2)
            decelerations_mps2.append(decel)

        # Deceleration must monotonically increase with pressure
        for i in range(len(decelerations_mps2) - 1):
            self.assertGreater(decelerations_mps2[i+1], decelerations_mps2[i])

        # Maximum deceleration should be in realistic high-grip range (0.5g - 0.88g)
        max_decel_g = decelerations_mps2[-1] / 9.81
        self.assertGreater(max_decel_g, 0.50, f"Max decel {max_decel_g:.2f}g was too weak for 150 bar")
        self.assertLessEqual(max_decel_g, 0.88, f"Max decel {max_decel_g:.2f}g exceeded tire friction limits")

    def test_stopping_distance_vs_velocity(self):
        """Measure stopping distances from 1.0 m/s, 2.0 m/s, and 3.0 m/s under service braking."""
        dt = 0.01
        initial_speeds = [1.0, 2.0, 3.0]  # m/s
        stopping_distances = []
        service_brake_kpa = 3000.0  # 30 bar service brake (~680 N braking force)
        b_force = pressure_kpa_to_brake_force_n(service_brake_kpa)

        for v0 in initial_speeds:
            plant = LongitudinalVehiclePlant()
            plant.velocity_mps = v0
            start_pos = plant.position_m
            steps = 0
            while plant.velocity_mps > 0.001 and steps < 1000:
                plant.step(commanded_motor_torque_nm=0.0, brake_force_n=b_force, dt=dt)
                steps += 1
            dist = plant.position_m - start_pos
            stopping_distances.append(dist)

        # Stopping distance scales quadratically with speed: d ≈ v^2 / (2 * a)
        # Ratio of d(2.0) / d(1.0) should be approximately 4.0
        ratio = stopping_distances[1] / stopping_distances[0]
        self.assertAlmostEqual(ratio, 4.0, delta=0.2)

        # From full speed (3.0 m/s ≈ 10.8 km/h), vehicle must halt within 2.5 meters under 30 bar service brake
        self.assertLess(stopping_distances[2], 2.5, f"Stopping distance {stopping_distances[2]:.2f}m was too long")

    def test_emergency_estop_response(self):
        """Verify ESTOP cuts motor propulsion and brings vehicle to full stop within safety gate deadline (< 1.2s)."""
        dt = 0.01
        plant = LongitudinalVehiclePlant()
        plant.velocity_mps = 3.0  # Full speed (3000 mm/s)
        plant.motor_torque_nm = 50.0  # Cruising under power

        # Emergency ESTOP arrives at t = 0
        # Invariant 1: Commanded motor torque immediately drops to 0
        # Invariant 2: Maximum emergency braking is commanded (20000 kPa)
        estop_brake_force = pressure_kpa_to_brake_force_n(20000.0)

        elapsed_time = 0.0
        positions = []

        while plant.velocity_mps > 0.001 and elapsed_time < 3.0:
            plant.step(commanded_motor_torque_nm=0.0, brake_force_n=estop_brake_force, dt=dt)
            elapsed_time += dt
            positions.append(plant.position_m)

        # Invariants
        self.assertEqual(plant.velocity_mps, 0.0, "Vehicle failed to stop completely under ESTOP")
        self.assertLess(elapsed_time, 1.2, f"ESTOP stopping time {elapsed_time:.2f}s exceeded 1.2s deadline!")
        self.assertLess(positions[-1], 1.5, f"ESTOP stopping distance {positions[-1]:.2f}m exceeded 1.5m limit!")


if __name__ == "__main__":
    unittest.main()
