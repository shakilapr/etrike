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

    def step(self, commanded_motor_torque_nm: float, gear: int = 1, brake_force_n: float = 0.0, road_grade_rad: float = 0.0, dt: float = 0.01) -> float:
        """Advance vehicle plant state by dt.
        
        gear: 1 for Drive (D), 3 for Reverse (R), 0 for Neutral (N).
        commanded_motor_torque_nm: motor torque magnitude [0..max_torque_nm].
        brake_force_n: hydraulic brake clamping force at contact patch [0..max].
        road_grade_rad: road grade angle (positive = uphill).
        """
        # 1. Non-instantaneous motor torque response (first-order lag)
        bounded_cmd = max(0.0, min(commanded_motor_torque_nm, self.cfg.max_torque_nm))
        self.motor_torque_nm += (dt / self.cfg.motor_tau_s) * (bounded_cmd - self.motor_torque_nm)

        # 2. Wheel tractive force (direction determined by transmission gear)
        t_wheel = self.motor_torque_nm * self.cfg.gear_ratio * self.cfg.drivetrain_efficiency
        f_drive_mag = t_wheel / self.cfg.wheel_radius_m
        if gear == 1:       # Drive: positive forward tractive force
            f_drive = f_drive_mag
        elif gear == 3:     # Reverse: negative backward tractive force
            f_drive = -f_drive_mag
        else:               # Neutral / Inhibited
            f_drive = 0.0

        # 3. Resistive forces (rolling, aerodynamic, and grade)
        v = self.velocity_mps
        # Rolling resistance opposes motion direction
        if abs(v) > 0.005:
            f_roll = math.copysign(self.cfg.c_rr * self.cfg.mass_kg * self.cfg.gravity * math.cos(road_grade_rad), v)
        else:
            f_roll = 0.0

        # Aerodynamic drag opposes motion direction
        f_drag_mag = 0.5 * self.cfg.air_density * self.cfg.c_d * self.cfg.frontal_area_m2 * (v ** 2)
        f_drag = math.copysign(f_drag_mag, v) if abs(v) > 0.005 else 0.0

        # Grade resistance (gravity component along slope)
        f_grade = self.cfg.mass_kg * self.cfg.gravity * math.sin(road_grade_rad)

        # Mechanical braking always opposes vehicle velocity
        f_brake_signed = math.copysign(max(0.0, brake_force_n), v) if abs(v) > 0.005 else 0.0

        # 4. Net force balance
        f_net = f_drive - f_roll - f_grade - f_drag - f_brake_signed

        # 5. Acceleration
        self.acceleration_mps2 = f_net / self.cfg.mass_kg

        # 6. Discrete integration with realistic standstill zero-crossing capture
        v_next = self.velocity_mps + self.acceleration_mps2 * dt

        # Zero-crossing check: friction and service brakes bring vehicle to rest without oscillating
        if brake_force_n > 0.0 and abs(f_drive) < 100.0:
            if (self.velocity_mps > 0.0 and v_next <= 0.0) or (self.velocity_mps < 0.0 and v_next >= 0.0) or abs(v_next) < 0.001:
                v_next = 0.0
                self.acceleration_mps2 = 0.0
        elif gear == 1 and self.velocity_mps >= 0.0 and v_next < 0.0 and road_grade_rad >= 0.0:
            # Prevent unnatural rolling backward on flat/uphill road in Drive
            v_next = 0.0
            self.acceleration_mps2 = 0.0
        elif gear == 3 and self.velocity_mps <= 0.0 and v_next > 0.0 and road_grade_rad <= 0.0:
            # Prevent forward creep when commanding reverse
            v_next = 0.0
            self.acceleration_mps2 = 0.0

        self.velocity_mps = v_next
        self.position_m += self.velocity_mps * dt
        return self.velocity_mps * 1000.0  # Return mm/s
