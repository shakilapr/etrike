# Brake System — End-to-End Function

The E-Trike uses a **brake-by-wire unit** electro-hydraulic brake actuator commanded via CAN. There is no mechanical cable from lever to master cylinder — the SEB is the sole hydraulic path. This document describes how braking works in each operating mode, how the two control modes (Stroke and Pressure) differ, and how multiple brake sources arbitrate to a single actuator.

---

## 1. Physical path

```
Brake lever (GPIO2, active-low) ──► SYS ESP32-S3
                                         │
                                    CAN 0x7B9 (50 Hz) ──► brake-by-wire unit ──► hydraulic master cylinder
                                                                              │
                                                                         calipers (front + rear)
```

---

## 2. Two control modes — same frame, different fields

`0x7B9 VCU_SEB_REQ` carries both stroke and pressure targets in every frame. The `VCU_SEB_Control_Mode` bit (byte 0, bit 2) selects which one the SEB acts on:

| Mode | Field | Type | What it commands | Used when |
|------|-------|------|-----------------|-----------|
| **Stroke (1)** | `VCU_SEB_Stroke_Value_Req` | u16, 0.05 mm/bit, offset -30 | Pushrod moves to an exact physical position | MANUAL lever, ESTOP, AUTO fallback |
| **Pressure (2)** | `VCU_SEB_Pre_Value_Req` | u8, 0.05 MPa/bit, 0–5 MPa | SEB's internal PID holds a target hydraulic pressure | AUTO modulated braking via `0x205` |

Both fields are present in every frame. The mode bit tells the SEB which one to obey. SYS can switch modes mid-operation by changing the bit — the SEB transitions on the next received frame.

**Why Pressure Mode is better for autonomous braking:** Stroke Mode commands a position. Pad wear, temperature, and fluid viscosity change the relationship between stroke and braking force. At 15 mm stroke with cold thick pads you get ~2 MPa; same 15 mm with hot thin pads you get ~0.5 MPa. Pressure Mode commands force directly — the SEB's internal PID moves the pushrod until the pressure sensor reads the target, compensating automatically.

---

## 3. MANUAL mode — binary braking

```
Rider squeezes lever
       │
       ▼  GPIO2 reads LOW (active-low)
       │
  brake_task (20 Hz): lever_pressed
       │
       ▼  send 0x7B9 {mode=Stroke, stroke=15mm} at 50 Hz
       │
  SEB pushrod → 15mm → hydraulic pressure builds → calipers clamp
       │
       ▼  brake_light GPIO21 = ON (OR logic)
```

When the rider releases, SYS sends `{mode=Stroke, stroke=0mm}`. The SEB releases pressure. The master cylinder spring-returns the pushrod.

Manual braking is **binary** — 15 mm or 0 mm. No modulation. The rider controls stopping distance by pulsing the lever, not by varying pressure. This is acceptable because in MANUAL mode the rider also controls speed via the throttle.

---

## 4. AUTO mode — three sources, one actuator

AUTO has three independent brake triggers converging on the same `0x7B9` frame. Priority order:

### Priority: ESTOP > lever > modulated > released

```
brake_task @ 20 Hz:

  if ESTOP:
      send 0x7B9 {mode=Stroke, stroke=27mm}   ← full lock

  elif lever_pressed:
      send 0x7B9 {mode=Stroke, stroke=15mm}   ← driver always wins

  elif g_brake_pressure_kpa > 0:              ← from 0x205 RT_BRAKE_CMD
      raw = (uint8_t)(kpa * 0.02)             ← 5000 kPa → raw=100 (5 MPa)
      raw = clamp(raw, 0, 100)
      send 0x7B9 {mode=Pressure, pressure=raw} ← modulated braking

  else:
      send 0x7B9 {mode=Stroke, stroke=0mm}    ← released
```

**The driver override is absolute.** If the rider squeezes the lever, SYS switches back to Stroke Mode at 15 mm regardless of what `0x205` commands. The human always wins.

### 4.1 Source 1: Brake lever (driver override)

Identical to MANUAL mode. The lever is a binary switch — pressed = 15 mm, released = 0 mm. Available in all modes.

### 4.2 Source 2: `0x205 RT_BRAKE_CMD` (modulated autonomous braking)

```
Jetson perception stack
       │
       ▼  "obstacle at 15m, decelerate at 300 kPa"
  0x301 HOST_BRAKE_REQ {300 kPa}  (high CAN, on demand)
       │
       ▼
RT dispatch_task:
   obstacle_distance = read HC-SR04
   rt_obstacle_kpa   = obstacle_to_brake(obstacle_distance)
   brake_kpa = max(rt_obstacle_kpa, jetson_301_kpa)
       │
       ▼
RT can_tx_low_task:
   send 0x205 {brake_kpa} at 50 Hz  (low CAN)
       │
       ▼
SYS dispatch_task:
   g_brake_pressure_kpa = 0x205 value  (atomic)
       │
       ▼
SYS brake_task:
   raw = (uint8_t)(kpa × 0.02)
   send 0x7B9 {mode=Pressure, pressure=raw}
       │
       ▼
SEB internal PID: holds target pressure
```

RT uses max-select arbitration: the worse (higher) pressure between Jetson's deceleration request and the obstacle sensor's emergency brake wins.

### 4.3 Source 3: ESTOP

Always Stroke Mode at 27 mm (maximum physical stroke). This path bypasses every software decision — it doesn't check `0x205`, doesn't check the lever, doesn't care about Pressure Mode. It's the emergency stop.

---

## 5. ESTOP mode — maximum brake, no questions

```
ESTOP trigger (button / CAN 0x001 / heartbeat timeout)
       │
       ▼
SYS brake_task:
   send 0x7B9 {mode=Stroke, stroke=27mm}   ← maximum physical stroke
       │
       ▼
SEB: full hydraulic pressure → maximum braking force
       │
       ▼
brake_light GPIO21 = ON (forced)
```

27 mm is the SEB's physical maximum (raw 1140). This is independent of all other brake sources.

---

## 6. Mode-switching protocol

### Stroke → Pressure (0x205 transitions 0 → positive)

```
1. SYS is in Stroke Mode, stroke=0mm (released)
2. 0x205 arrives with positive kPa value
3. SYS: hold current stroke position (already released)
4. SYS: switch 0x7B9 mode bit from 1 (Stroke) to 2 (Pressure)
5. SYS: set VCU_SEB_Pre_Value_Req to converted raw value
6. SEB: receives frame, sees mode=Pressure, starts PID loop from 0 MPa → target
```

### Pressure → Stroke (0x205 drops to 0)

```
1. SYS: switch mode bit from 2 (Pressure) back to 1 (Stroke)
2. SYS: set stroke=0mm (released)
3. SEB: releases pressure
```

The hold-then-switch pattern prevents pressure transients — the SEB doesn't jump between mode interpretations of the same command bytes.

---

## 7. Conversion formula

```
kPa → SEB raw:  raw = kPa × 0.02

Constants:
  kSebPressureScale  = 0.02f       (1 bit = 0.05 MPa; 1 MPa = 1000 kPa)
  kSebPressureOffset = 0.0f
  kSebMaxPressureRaw = 100         (100 × 0.05 = 5.0 MPa hardware limit)

Example conversions:
  1000 kPa → 1000 × 0.02 = 20   → 1.0 MPa (gentle deceleration)
  3000 kPa → 3000 × 0.02 = 60   → 3.0 MPa (firm stop)
  5000 kPa → 5000 × 0.02 = 100  → 5.0 MPa (maximum, emergency)
```

Verified against brake-by-wire unit CAN protocol specification. `VCU_SEB_Pre_Value_Req` is u8 at bit 32 of `0x7B9`, scale 0.05 MPa/bit, range 0–5 MPa.

---

## 8. Stroke value reference

```
raw = (physical_mm + 30.0) / 0.05

| Physical | Raw  | Use case              |
|----------|------|-----------------------|
| -5 mm    | 500  | Minimum               |
| 0 mm     | 600  | Released              |
| 15 mm    | 900  | Manual lever pressed  |
| 27 mm    | 1140 | ESTOP full brake      |
```

---

## 9. Brake light logic

```
brake_light_on = lever_pressed()           // GPIO2 — physical switch
              OR (mode == ESTOP)           // emergency
              OR g_light_state.brake_light // Jetson 0x302 (predictive/hazard)
```

All three sources are local to SYS — no CAN round-trip needed for the lever or ESTOP. The Jetson bit is supplemental (predictive illumination before pressure builds) and can never be the only trigger. Physical braking state always wins.

---

## 10. Boot sequence

```
Power-on
    │
    ▼
BRAKE_BOOT_WAIT (500ms) — do NOT transmit 0x7B9
    │
    ▼
BRAKE_LISTEN_SYNC:
    Wait for 0x721 SEB_STATUS from SEB
    Read current stroke position (SEB_Stroke_Value)
    Set initial command = current position (hold — don't move)
    Wait for SEB_Alignment_Status == 1
    Timeout: 2s → BRAKE_FAULT
    │
    ▼
BRAKE_ACTIVE: transmit 0x7B9 at 50 Hz continuously
```

If the SEB never responds → `BRAKE_FAULT`. SYS stops transmitting `0x7B9`. The SEB enters its own timeout-fault. The rider has no hydraulic braking. This is a known single-point failure — there is currently no mechanical brake fallback (tracked as issue M2).

---

## 11. Complete brake source summary

| Path | Trigger | Mode | Command | When |
|------|---------|------|---------|------|
| Lever | Rider squeezes GPIO2 | Stroke (1) | 15 mm | MANUAL, AUTO (always overrides) |
| ESTOP | Button / CAN `0x001` / HB loss | Stroke (1) | 27 mm (max) | Any mode |
| Jetson decel | Perception → `0x301` → RT → `0x205` | Pressure (2) | 0–5 MPa | AUTO only, no lever |
| Obstacle | HC-SR04 → RT → `0x205` | Pressure (2) | 0–5 MPa | AUTO only, no lever |
| Released | No lever, no `0x205`, no ESTOP | Stroke (1) | 0 mm | AUTO default |

---

*See also: [`architecture.md`](../architecture.md) §8.6 for brake control mechanisms, [`can-dictionary.md`](../can-dictionary.md) §0x7B9 and §0x721 for bit-level frame layouts (also §0x731 SEB_ErrInfo, §0x741 SEB_Version, §0x6FB SEB_Test), [`docs/brake-unit.md`](brake-unit.md) for brake-by-wire unit protocol reference, [`issues.md`](../issues.md) M2 for the mechanical fallback gap.*
