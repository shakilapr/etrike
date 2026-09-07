"""Deterministic Closed-Loop Longitudinal Vehicle Plant Simulation Model (§8.1 & §8.2).

Implements the vehicle dynamic plant:
- Tractive wheel force: F_drive = T_wheel / r_w
- Rolling resistance:   F_roll = C_rr * m * g * cos(theta)
- Aerodynamic drag:     F_drag = 0.5 * rho * C_d * A * v^2
- Grade resistance:     F_grade = m * g * sin(theta)
- Mechanical braking:   F_brake
- Discrete state integration: delta_t = 0.01 s
"""

import math
from dataclasses import dataclass


@dataclass
class PlantConfig:
    mass_kg: float = 350.0            # Vehicle curb + driver mass
    wheel_radius_m: float = 0.25      # 250 mm rolling radius
    gear_ratio: float = 8.5           # Transmission reduction
    drivetrain_efficiency: float = 0.90
    c_rr: float = 0.015               # Rolling resistance coefficient
    c_d: float = 0.7                  # Aerodynamic drag coefficient
    frontal_area_m2: float = 1.2      # Frontal area
    air_density: float = 1.225        # kg/m^3
    gravity: float = 9.81             # m/s^2
    motor_tau_s: float = 0.05         # Motor first-order lag (50 ms)
    max_torque_nm: float = 80.0       # Peak motor torque
    max_power_w: float = 4000.0       # Peak electrical power


class LongitudinalVehiclePlant:
    def __init__(self, config: PlantConfig = PlantConfig()):
        self.cfg = config
        self.velocity_mps = 0.0
        self.position_m = 0.0
        self.motor_torque_nm = 0.0
        self.acceleration_mps2 = 0.0

    def step(self, commanded_motor_torque_nm: float, brake_force_n: float = 0.0, road_grade_rad: float = 0.0, dt: float = 0.01) -> float:
        # 1. Non-instantaneous motor torque response (first-order lag)
        # Torque saturation
        bounded_cmd = max(0.0, min(commanded_motor_torque_nm, self.cfg.max_torque_nm))
        self.motor_torque_nm += (dt / self.cfg.motor_tau_s) * (bounded_cmd - self.motor_torque_nm)

        # 2. Wheel tractive force
        t_wheel = self.motor_torque_nm * self.cfg.gear_ratio * self.cfg.drivetrain_efficiency
        f_drive = t_wheel / self.cfg.wheel_radius_m

        # 3. Resistive forces
        v = self.velocity_mps
        f_roll = self.cfg.c_rr * self.cfg.mass_kg * self.cfg.gravity * math.cos(road_grade_rad) if v > 0.01 else 0.0
        f_grade = self.cfg.mass_kg * self.cfg.gravity * math.sin(road_grade_rad)
        f_drag = 0.5 * self.cfg.air_density * self.cfg.c_d * self.cfg.frontal_area_m2 * (v ** 2)

        # 4. Net force balance
        f_net = f_drive - f_roll - f_grade - f_drag - brake_force_n

        # 5. Acceleration
        self.acceleration_mps2 = f_net / self.cfg.mass_kg

        # 6. Discrete Euler-Cromer integration
        self.velocity_mps += self.acceleration_mps2 * dt
        if self.velocity_mps < 0.0:
            self.velocity_mps = 0.0 # Prevent unnatural reverse roll in simple forward drive
            self.acceleration_mps2 = 0.0

        self.position_m += self.velocity_mps * dt
        return self.velocity_mps * 1000.0 # Return mm/s
