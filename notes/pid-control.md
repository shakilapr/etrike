# PID Control Fundamentals

A **PID controller** closes the loop between a desired value (setpoint) and a measured value (process variable), producing an output that drives the error toward zero. It's the workhorse of embedded control — motor speed, temperature, position, voltage — anywhere you need a system to track a target.

The E-Trike uses PID on the RT ESP32-S3 for rear-motor speed control. Jetson says "go 1500 mm/s", RT measures the actual speed from the encoder, the PID computes a DAC value for the motor controller, and the loop repeats at 100 Hz.

---

## 1. The Closed-Loop Concept

```
                    ┌──────────┐
setpoint ────►[+]──►│   PID    │──► output ──► [Plant / Motor] ──► actual speed
              ▲     └──────────┘                                    │
              │                                                     │
              └────────────────── measurement ◄─────────────────────┘
```

Without feedback (open-loop): you command 50% throttle, the vehicle does… something. Uphill it crawls, downhill it speeds. The controller has no idea.

With feedback (closed-loop): the PID sees the error and adjusts. Uphill → error grows → integral term ramps up → throttle increases until the error is zero.

---

## 2. The Three Terms

### Proportional (P)

```
P_out = Kp × error
```

- **What it does:** Reacts to the *present* error. The further from target, the harder it pushes.
- **Intuition:** Like a spring — the force is proportional to how far you've stretched it.
- **Too high:** Oscillation. The controller overshoots, corrects, overshoots the other way.
- **Too low:** Sluggish. The vehicle takes too long to reach target speed.
- **Alone:** Never reaches the target exactly. There's always a small residual error (steady-state offset) because as error → 0, P_out → 0.

### Integral (I)

```
integral += error × dt
integral = clamp(integral, −max, +max)
I_out = Ki × integral
```

- **What it does:** Accumulates *past* error. If the vehicle cruises 50 mm/s below target for 2 seconds, the integral term grows until the offset is eliminated.
- **Intuition:** Like a water bucket under a dripping faucet — the longer the error persists, the fuller the bucket, and the stronger the correction.
- **Why clamping matters (anti-windup):** If the motor can't physically reach the target (uphill, headwind), the integral grows without bound. When the load reduces, the inflated integral causes a massive overshoot. **Clamp the integral** to prevent this.
- **When to reset:** On mode change (ESTOP→MANUAL) or on large setpoint changes, zero the integral. Otherwise, accumulated error from the previous operating point causes a transient surge.

### Derivative (D)

```
derivative = (error − prev_error) / dt
D_out = Kd × derivative
```

- **What it does:** Reacts to the *rate of change* of error. If the vehicle is approaching the target quickly, the D term backs off to prevent overshoot.
- **Intuition:** Like a shock absorber — it resists rapid changes. The faster you approach, the harder it pushes back.
- **Noise problem:** The derivative amplifies measurement noise. A 1-bit encoder jitter looks like a massive instantaneous speed change.

**Derivative on measurement (the fix):**

Instead of differentiating the error (which jumps on setpoint changes), differentiate the measurement directly:

```cpp
// Derivative on error — causes "derivative kick" on setpoint step
derivative = (error - prev_error) / dt;

// Derivative on measurement — smooth response to setpoint changes
derivative = -(measurement - prev_measurement) / dt;
```

The setpoint only enters through the P and I terms. The D term only damps actual system motion. This avoids a spike in output when the setpoint changes abruptly.

---

## 3. The Complete PID Equation

```cpp
float pid_update(float setpoint, float measurement, float dt) {
    float error = setpoint - measurement;

    // Proportional — present error
    float p_term = Kp * error;

    // Integral — accumulated past error (with anti-windup)
    integral += error * dt;
    integral = clamp(integral, -max_integral, +max_integral);
    float i_term = Ki * integral;

    // Derivative on measurement — rate of change (no kick)
    float derivative = -(measurement - prev_measurement) / dt;
    float d_term = Kd * derivative;

    prev_measurement = measurement;

    return clamp(p_term + i_term + d_term, out_min, out_max);
}
```

---

## 4. Tuning — Making It Work

Tuning is finding Kp, Ki, Kd values that give fast response without oscillation.

### Manual tuning procedure

1. **Set Ki = Kd = 0.** Start with P only.
2. **Increase Kp** until the system oscillates with *constant amplitude* (neither growing nor decaying). This is the **ultimate gain** Ku.
3. **Set Kp = 0.6 × Ku.** This gives a stable proportional response.
4. **Add I.** Increase Ki until steady-state error is eliminated within ~2 seconds of a step change. Watch for overshoot — if the system overshoots and slowly settles, Ki is too high.
5. **Add D (if needed).** Increase Kd to dampen overshoot. On a high-inertia system (vehicle), D helps. On a low-inertia system, D often amplifies noise — skip it.

### What good tuning looks like

```
Speed ▲
      │        ┌──────────────  setpoint
      │       ╱
      │      ╱                 ← slight overshoot (5-10%), quick settling
      │     ╱
      │    ╱
      │   ╱
      │  ╱
      │ ╱
      │╱
      └────────────────────────► time
```

- **Rise time** (10%→90%): fast but not aggressive
- **Overshoot**: <10% of setpoint step
- **Settling time** (±5% of setpoint): <1 second
- **No sustained oscillation** at steady state

---

## 5. The E-Trike's PID in Context

The PID on RT is the **outermost speed loop**:

```
Jetson cmd_vel ──► Kinematics ──► speed setpoint ──► PID ──► DAC value ──► Motor Controller ──► Motor
                         ▲                               │
                         │                               │
                         └── encoder feedback ◄──────────┘
```

The motor controller (a separate unit) runs its own internal current/torque loop at 10–20 kHz. The E-Trike's PID only needs to be fast enough to track vehicle-speed dynamics — 100 Hz is more than sufficient for a ~300 kg tricycle with seconds-scale acceleration.

### What the PID does NOT control

- **Steering angle** — EPS-C runs its own internal position loop. RT commands an angle; the unit's firmware handles the motor.
- **Brake pressure** — SEB runs its own internal pressure/stroke loop. SYS commands a stroke; the unit handles the pump.
- **Motor commutation** — The motor controller handles FOC (Field-Oriented Control) internally. RT only sends a 0–5 V speed reference.

---

## 6. When PID Isn't Enough

PID is the right tool for single-input, single-output (SISO) systems with linear-ish behavior. You need more when:

| Situation | Better approach |
|-----------|----------------|
| Multiple interacting variables (speed + steering) | MIMO control, MPC |
| Large dead time (seconds of delay) | Smith predictor |
| Highly nonlinear plant | Gain scheduling, adaptive control |
| Hard constraints (must never exceed limit) | MPC with constraints |

For a tricycle speed controller, PID is the right choice — simple, well-understood, and sufficient.

---

*See also: [[physics-model]] for the kinematics that produce the speed setpoint, [[actuator-interfacing]] for the MCP4725 DAC, `architecture.md` §7.6 for the control loop integration.*
