# PID Speed Control

The RT ESP32-S3 runs a **PID controller** at 100 Hz to track the speed setpoint computed by the tricycle kinematics solver. This closes the loop between the desired speed (from Jetson or physics model) and the actual speed (measured from the rear motor encoder).

---

## Why PID?

A PID (Proportional-Integral-Derivative) controller is the standard choice for embedded motor speed control because:

1. **Simple to implement:** Three terms, multiply-accumulate, no matrix math.
2. **Well understood:** Tuning procedures are mature, behavior is predictable.
3. **Sufficient for the problem:** Motor speed control on a tricycle doesn't require model-predictive control or adaptive tuning — the load (rider + vehicle mass) is fairly constant, and the motor controller's internal current loop handles the fast dynamics.

---

## PID loop structure

```
                    ┌──────────┐
setpoint_speed ────►│    +     │────► P × error ────────────────┐
                    │   error  │                                │
measured_speed ────►│    -     │────► I × ∫error ───────────────┤───► output ──► MCP4725 DAC
                    └──────────┘                                │
                               └────► D × d(error)/dt ──────────┘
```

### Proportional term (P)

```
P_out = Kp × error
```

- **What it does:** Produces an output proportional to the current error. The further from target, the harder it pushes.
- **Too high:** Oscillation. The controller overshoots, corrects, overshoots the other way.
- **Too low:** Sluggish response. The vehicle takes too long to reach target speed.

### Integral term (I)

```
integral += error × dt
integral = clamp(integral, -max_integral, +max_integral)
I_out = Ki × integral
```

- **What it does:** Accumulates past error to eliminate steady-state offset. If the vehicle is cruising 50 mm/s below target, the I term slowly ramps up until the error is zero.
- **Integral windup:** If the motor can't physically reach the target (e.g., uphill against max power), the integral grows without bound. When the load reduces, the inflated integral causes a massive overshoot. **Clamping** the integral prevents this.
- **Resetting on mode change:** When switching from ESTOP back to AUTO (after power-cycle), the integral must be zeroed. Otherwise, the accumulated error from the ESTOP period would cause a throttle surge.

### Derivative term (D)

```
derivative = (error - previous_error) / dt
D_out = Kd × derivative
```

- **What it does:** Reacts to the rate of change of error. If the vehicle is approaching the target quickly, the D term backs off to prevent overshoot.
- **Noise sensitivity:** The derivative amplifies measurement noise (encoder jitter). In practice, it's often computed on the measurement directly (`-Kd × d(measurement)/dt`) rather than on the error — this is called "derivative on measurement" and avoids derivative kicks on setpoint changes.

---

## Current tuning parameters

```cpp
constexpr float kPidKp = 1.0f;
constexpr float kPidKi = 0.1f;
constexpr float kPidKd = 0.05f;
constexpr float kPidMaxIntegral = 500.0f;
constexpr int   kControlLoopHz = 100;
```

| Parameter | Value | Unit |
|-----------|-------|------|
| Kp | 1.0 | DAC counts per mm/s error |
| Ki | 0.1 | DAC counts per mm/s·s accumulated error |
| Kd | 0.05 | DAC counts per mm/s² error rate |
| Max integral | ±500 | DAC counts |
| Loop rate | 100 | Hz |

> **Note:** These gains are initial values. They assume the MCP4725 DAC output range (0–4095) maps linearly to the motor controller's speed range. Actual tuning requires closed-loop testing with the real motor and load.

---

## Implementation

```cpp
class SpeedPID {
public:
    void reset() {
        integral_ = 0.0f;
        prev_error_ = 0.0f;
        prev_measurement_ = 0.0f;
    }

    float update(float setpoint, float measurement, float dt) {
        // Compute error
        float error = setpoint - measurement;

        // Proportional
        float p_term = Kp_ * error;

        // Integral (with anti-windup clamping)
        integral_ += error * dt;
        integral_ = std::clamp(integral_, -max_integral_, max_integral_);
        float i_term = Ki_ * integral_;

        // Derivative on measurement (reduces derivative kick)
        float derivative = -(measurement - prev_measurement_) / dt;
        float d_term = Kd_ * derivative;

        // Sum and clamp output
        float output = p_term + i_term + d_term;
        output = std::clamp(output, 0.0f, 4095.0f);  // DAC range

        // Store state for next iteration
        prev_error_ = error;
        prev_measurement_ = measurement;

        return output;
    }

private:
    float Kp_ = 1.0f;
    float Ki_ = 0.1f;
    float Kd_ = 0.05f;
    float max_integral_ = 500.0f;
    float integral_ = 0.0f;
    float prev_error_ = 0.0f;
    float prev_measurement_ = 0.0f;
};
```

The PID runs at 100 Hz in the RT `control` task:

```cpp
void control_task() {
    TickType_t last_wake = xTaskGetTickCount();
    SpeedPID pid;
    float dt = 1.0f / 100.0f;  // 100 Hz = 10 ms

    while (true) {
        DriveCmd cmd;
        if (xQueueReceive(cmd_queue, &cmd, 0) == pdTRUE) {
            // Physics resolve: cmd → setpoint (steer angle, motor speed)
            ResolvedSetpoint sp = physics_resolve(cmd);
            setpoint = sp;
        }

        // Read current speed from encoder
        float measured_speed = encoder_read_speed_mmps();

        // PID update
        float dac_value = pid.update(setpoint.motor_speed_mmps,
                                      measured_speed, dt);

        // Push to downstream (0x204 for MTR + 0x169 for EPS-C)
        ActuatorOutput out = {
            .dac_value = static_cast<uint16_t>(dac_value),
            .gear = setpoint.gear,
            .steer_angle_mdeg = setpoint.steer_angle_mdeg,
        };
        xQueueOverwrite(setpoint_queue, &out);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}
```

---

## Tuning procedure

For the tricycle's rear motor (brushless DC, ~1–3 kW, controlled by an external motor controller that accepts 0–5 V analog input):

1. **Start with P only.** Set Ki = Kd = 0. Increase Kp until the speed oscillates with constant amplitude. Set Kp to ~60% of that value.

2. **Add I.** Increase Ki until the steady-state error is eliminated within ~2 seconds of a step change. Watch for overshoot — if the vehicle overshoots and then slowly settles, Ki is too high.

3. **Add D (optional).** Increase Kd to dampen overshoot from step changes. On a vehicle with significant inertia, D can help. On a low-inertia system, D often amplifies encoder noise and is counterproductive.

4. **Test load rejection.** On a slight incline, command a constant speed. The I term should compensate for the additional load without oscillation.

5. **Test step response.** Command 0 → 1500 mm/s. Measure rise time (10–90%), overshoot (%), and settling time (±5%).

---

## What the PID does NOT control

- **Steering angle:** EPS-C runs its own internal position loop. RT commands an angle target; EPS-C controls the motor to reach it.
- **Brake pressure:** SEB runs its own internal pressure/stroke loop. SYS commands a stroke target; SEB controls the pump.
- **Motor commutation:** The motor controller handles FOC (Field-Oriented Control) internally. RT only sends a 0–5 V speed reference.

The PID is the outermost speed loop. The motor controller's internal current/torque loop is faster (typically 10–20 kHz) and handles the actual motor phase control.

---

*See also: [[physics-model]] for the tricycle kinematics that produce the speed setpoint, [[actuator-interfacing]] for the MCP4725 DAC that converts PID output to voltage, [[achitecture]] §7.6 for the control loop integration.*
