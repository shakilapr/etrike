# Physics Model for the Tricycle Platform

This document defines the vehicle model used by `rt-esp32/src/physics_model.cpp`.
It is the tricycle kinematic model, with an implementation focused on converting
ROS-style motion commands into steering and rear-motor setpoints.

## 1. Scope

The current firmware uses a rigid-body, planar, non-holonomic model:

- rear axle midpoint is the reference point,
- the front wheel provides steering,
- the rear axle provides propulsion,
- wheel slip is ignored in the base model.

This is sufficient for low-speed motion control, path tracking, and odometry
conversion. It does not model tire deformation, load transfer, or rollover
dynamics unless those are added separately.

## 2. State, inputs, and geometry

State vector:

$$
\mathbf{q} = \begin{bmatrix} x \\ y \\ \theta \end{bmatrix}
$$

where:

- `x`, `y` are the coordinates of the rear-axle midpoint in the global frame,
- `theta` is the vehicle heading.

Control inputs:

$$
\mathbf{u} = \begin{bmatrix} v \\ \delta \end{bmatrix}
$$

where:

- `v` is the forward linear velocity,
- `delta` is the front-wheel steering angle.

Geometric parameters:

- `L` = wheelbase,
- `w` = rear track width,
- `r_f` = front wheel radius,
- `r_r` = rear wheel radius.

Current RT configuration uses:

- `L = 1500 mm`
- `w = 800 mm`
- `r_r = 200 mm`
- steering limit = `45 deg`
- low-speed threshold = `50 mm/s`

## 3. Base tricycle kinematics

For pure rolling and no lateral slip, the forward kinematics are:

$$
\dot{x} = v \cos(\theta)
$$

$$
\dot{y} = v \sin(\theta)
$$

$$
\dot{\theta} = \frac{v}{L} \tan(\delta)
$$

The non-holonomic constraint is:

$$
\dot{x} \sin(\theta) - \dot{y} \cos(\theta) = 0
$$

This states that the vehicle cannot move sideways instantaneously.

## 4. Inverse steering solve used by the firmware

The RT ESP32 does not command wheel angles directly from `cmd_vel`.
Instead, it resolves the desired motion into:

- rear motor speed,
- front steering angle,
- validity flags for control logic.

The expected input type is the `DriveCmd` struct:

```cpp
struct DriveCmd {
    int32_t speed_mmps = 0;      // linear.x  [mm/s]
    int32_t yaw_rate_mrad_s = 0; // angular.z [millirad/s]
};
```

For normal motion, steering is computed from the requested yaw rate and speed:

$$
\delta = \arctan\left(\frac{L \omega}{|v|}\right)
$$

where `omega` is the commanded yaw rate.

The implementation uses `atan2(L * omega, abs(v))` so the sign comes from the
yaw-rate command and the zero-speed case stays numerically stable.

### Low-speed behavior

When `|v|` falls below the low-speed threshold, the steering angle is not
re-estimated from the command. The firmware holds the last valid steering angle
and decays it toward zero. This avoids noisy steering changes near standstill.

### Output limits

The solver clamps:

- forward speed to `kMaxSpeedFwdMmps`,
- reverse speed to `kMaxSpeedRevMmps`,
- steering to `kSteerLimitDeg`.

The resolved output is the `ResolvedSetpoint` struct:

```cpp
struct ResolvedSetpoint {
    int32_t motor_speed_mmps = 0; // rear motor target [mm/s]
    int32_t steer_angle_mdeg = 0; // front steer angle [millideg]
    bool steer_valid = false;
    bool reversing = false;
};
```

## 5. Wheel-level kinematics

If the rear wheels are treated separately, the platform behaves like a
differential-drive odometry model on the rear axle.

Rear wheel linear speeds:

$$
v_l = \omega_{rl} r_r
$$

$$
v_r = \omega_{rr} r_r
$$

Vehicle speed and yaw rate:

$$
v = \frac{v_r + v_l}{2}
$$

$$
\dot{\theta} = \frac{v_r - v_l}{w}
$$

Forward odometry in the global frame:

$$
\dot{x} = \frac{r_r(\omega_{rr} + \omega_{rl})}{2}\cos(\theta)
$$

$$
\dot{y} = \frac{r_r(\omega_{rr} + \omega_{rl})}{2}\sin(\theta)
$$

$$
\dot{\theta} = \frac{r_r(\omega_{rr} - \omega_{rl})}{w}
$$

## 6. Rear-wheel inverse kinematics

For a commanded `v` and `delta`, the turn radius is:

$$
R = \frac{L}{\tan(\delta)}
$$

That gives the rear-wheel path speeds:

$$
v_l = v \left(1 - \frac{w}{2L}\tan(\delta)\right)
$$

$$
v_r = v \left(1 + \frac{w}{2L}\tan(\delta)\right)
$$

Converted to wheel angular velocity:

$$
\omega_{rl} = \frac{v_l}{r_r}
$$

$$
\omega_{rr} = \frac{v_r}{r_r}
$$

This matters when the rear axle is driven by independent motors. With a solid
axle and differential, the hardware resolves the speed difference mechanically.

## 7. Front-wheel driven form

If the front wheel is driven and measured directly, its linear speed is:

$$
v_f = \omega_f r_f
$$

The rear-axle forward speed becomes:

$$
v = v_f \cos(\delta)
$$

and the state derivatives are:

$$
\dot{x} = (\omega_f r_f)\cos(\delta)\cos(\theta)
$$

$$
\dot{y} = (\omega_f r_f)\cos(\delta)\sin(\theta)
$$

$$
\dot{\theta} = \frac{\omega_f r_f}{L}\sin(\delta)
$$

## 8. Dynamic limits and rollover

The kinematic model is not enough at higher speeds. A tricycle has a high center
of gravity and a narrow track, so lateral acceleration can trigger rollover.

Lateral acceleration during a turn:

$$
a_y = v\dot{\theta} = \frac{v^2}{L}\tan(\delta)
$$

The rollover threshold is approximately:

$$
\frac{v^2}{L}\tan(\delta) > \frac{g w}{2h}
$$

where:

- `m` = vehicle mass,
- `h` = center-of-gravity height,
- `w` = rear track width,
- `g` = gravity.

For an advanced controller, this inequality should be enforced as a control
constraint so the command never enters an unstable region.

## 9. Slip-angle model

At higher speed, tire slip becomes non-negligible. A linearized tire model can
use slip angles and cornering stiffness:

$$
\alpha_f = \delta - \arctan\left(\frac{\dot{y} + L_f \dot{\theta}}{\dot{x}}\right)
$$

$$
\alpha_r = -\arctan\left(\frac{\dot{y} - L_r \dot{\theta}}{\dot{x}}\right)
$$

Lateral forces:

$$
F_{yf} = C_{\alpha f}\alpha_f
$$

$$
F_{yr} = C_{\alpha r}\alpha_r
$$

Vehicle dynamics:

$$
m(\ddot{y} + \dot{x}\dot{\theta}) = F_{yf}\cos(\delta) + F_{yr}
$$

$$
I_z \ddot{\theta} = L_fF_{yf}\cos(\delta) - L_rF_{yr}
$$

This model is useful for higher-fidelity estimation or model-predictive control,
but it is not required for the current RT ESP32 solver.

## 10. Obstacle speed limiting

The physics module also provides a simple obstacle-based speed limiter:

- stop completely at or below `kObstacleStopDistMM`,
- pass through target speed at or beyond `kObstacleClearDistMM`,
- interpolate linearly in between.

This is a separate safety layer from steering geometry.

## 11. Practical summary

Use the base tricycle model for:

- command resolution,
- low-speed trajectory generation,
- basic odometry,
- steering actuation.

Add the dynamic model only when the controller must reason about:

- tire slip,
- high-speed cornering,
- load transfer,
- rollover risk.
