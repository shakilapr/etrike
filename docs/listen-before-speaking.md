# Listen Before Speaking (LBS) — Safe CAN Actuator Bootstrapping

When a CAN actuator powers on, it has no idea what state the vehicle is in. If the controller immediately sends a position command, the actuator will jerk to that target — potentially causing a dangerous steering or brake transient.

**Listen Before Speaking (LBS)** is the pattern that prevents this. The controller waits for the actuator's first status frame, reads the *current physical position*, and sets its initial command to match. Only then does it begin transmitting commands.

This is used for both SYNTREE actuators on our tricycle.

---

## Why LBS is necessary

CAN actuators are stateful devices that maintain their own internal control loops:

- **EPS-C** (steering): runs an internal position loop. If you send `0x169` with angle=0° and the wheel is physically at 15°, it will slew to 0° at maximum motor speed. This can yank the handlebars out of the rider's hands.
- **SEB** (brake): runs an internal pressure/stroke loop. If you command stroke=0 while the calipers are partially engaged, the brake releases abruptly.

Both actuators also **require continuous 50 Hz command frames** once active. If frames stop for >20 ms, the actuator enters a comm-fault timeout (lock, limp, or freewheel — behavior is unit-specific).

---

## The LBS state machine

```
Power-on
    │
    ▼
┌──────────────┐
│  BOOT_WAIT   │  500 ms delay — do NOT transmit any command frames
│              │  Actuator is powering up, may not be ready
└──────┬───────┘
       │ 500 ms elapsed
       ▼
┌──────────────┐
│ LISTEN_SYNC  │  Wait for first status frame (0x201 or 0x721)
│              │  Extract current physical position (angle or stroke)
│              │  Set active_target = current_physical_position
│              │  Wait for alignment_status bit == 1
└──────┬───────┘
       │ Status received + aligned
       ▼
┌──────────────┐
│   ACTIVE     │  Transmit command at 50 Hz continuously
│              │  First frame commands "stay where you are"
│              │  Then follow controller targets with rate limiting
└──────┬───────┘
       │ Timeout (no status for >2s) or ESTOP
       ▼
┌──────────────┐
│    FAULT     │  Stop transmitting
│              │  Actuator will timeout-fault internally
└──────────────┘
```

---

## EPS-C (Steering) specifics

| Parameter | Value |
|-----------|-------|
| Status ID | `0x201` SES_STATUS (100 Hz from EPS-C) |
| Position field | `SES_StrAngle` (int16, 0.1°/bit) |
| Alignment field | `SES_INF_Angle_Status` (bit 0 of status byte) |
| Command ID | `0x169` VCU_SES_REQ (50 Hz to EPS-C) |
| Boot wait | 500 ms |
| Sync timeout | 2 seconds → STEER_FAULT, remain in MANUAL |
| Mode behavior | MANUAL: do NOT send `0x169`, EPS-C standalone. AUTO: send at 50 Hz. ESTOP: stop sending. |

### Steering LBS in C++

```cpp
enum class SteerState {
    STEER_BOOT_WAIT,    // 500 ms power-on delay
    STEER_LISTEN_SYNC,  // Wait for 0x201, read angle, wait for aligned
    STEER_ACTIVE,       // Normal operation, transmit 0x169 at 50 Hz
    STEER_FAULT         // Timeout or ESTOP, stop transmitting
};

void steer_state_machine() {
    switch (state) {
    case STEER_BOOT_WAIT:
        if (millis_since_boot() > 500) {
            state = STEER_LISTEN_SYNC;
        }
        break;

    case STEER_LISTEN_SYNC:
        if (received_0x201) {
            // CRITICAL: match current physical position
            int16_t current_angle_raw = ses_status.angle_raw;
            active_target_angle = raw_to_mdeg(current_angle_raw);
            if (ses_status.alignment_ok) {
                state = STEER_ACTIVE;
            }
        } else if (time_in_state > 2000) {
            state = STEER_FAULT;
        }
        break;

    case STEER_ACTIVE:
        send_0x169_at_50hz();
        // Monitor following error
        if (abs(cmd_angle - actual_angle) > 5000_mdeg &&
            error_duration_ms > 300) {
            mode_set(Estop);  // stuck linkage
        }
        break;

    case STEER_FAULT:
        // Silent — let EPS-C timeout-fault internally
        break;
    }
}
```

---

## SEB (Brake) specifics

| Parameter | Value |
|-----------|-------|
| Status ID | `0x721` SEB_STATUS (100 Hz from SEB) |
| Position field | `SEB_Stroke_Value` (uint16, scale 0.05, offset -30) |
| Alignment field | `SEB_Alignment_Status` |
| Command ID | `0x7B9` VCU_SEB_REQ (50 Hz to SEB) |
| Boot wait | 500 ms |
| Sync timeout | 2 seconds → BRAKE_FAULT, brake lever inoperative until resolved |

The brake LBS is identical in structure to steering but runs on the SYS ESP32-S3 instead of RT.

---

## Why 500 ms boot wait?

SYNTREE actuators run an internal bootloader and self-test on power-up. During this window:

- The CAN interface may not be ready (no ACK, no status frames).
- The internal position sensor may not be calibrated.
- Sending commands during boot can trigger a fault latch.

500 ms is conservative for these units. Shorter times risk startup faults; longer times delay readiness.

---

## Why this matters for safety

Without LBS, the worst case on power-up:

1. Vehicle was parked with steering at full right lock (~40°).
2. Controller boots faster than EPS-C and immediately commands 0°.
3. EPS-C slews 40° at max motor speed — handlebars whip to center.
4. If a rider is mounting the vehicle, this can cause injury.

LBS guarantees the first command is always "stay exactly where you are."

---

## Comm-fault timeout (the other side of LBS)

The continuous 50 Hz requirement is the **speaking** obligation once LBS completes:

- EPS-C: if `0x169` stops for >20 ms → internal comm fault → locks or goes limp (TBD by unit spec).
- SEB: if `0x7B9` stops → similar timeout.

This means the controller has an ongoing duty to transmit — silence is interpreted as failure. The actuator's internal safety logic handles the timeout independently; the controller doesn't need to send an explicit "stop" command.

---

*Primary reference: [[emergency-system]] for ESTOP behavior, watchdog recovery sequence (which re-runs LBS), and testing procedures that exercise the LBS state machine.*

*See also: [[defense-in-depth-safety]] for following error and dynamic angle clamp, [[syntree-security-protocol]] for rolling counter + checksum, [[architecture]] §7.6 for the steering boot sequence, §8.6 for the brake boot sequence.*
