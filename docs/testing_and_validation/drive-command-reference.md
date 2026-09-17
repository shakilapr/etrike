# Drive Command Reference — how to make the vehicle move

**Applies to:** RT-ESP32 + SYS-ESP32 firmware (protocol `etrike.application.v1`).
CAN 2.0, 500 kbit/s. High bus = `0x…` host/RT domain, Low bus = `0x…` actuators
(MTR / SES / SEB) + SYS.

> On the **hardware bench** (actuator-less) MTR/SES/SEB are absent, so "moving"
> means *the correct frames appear on the Low bus*. On the **vehicle** the same
> frames drive the real actuators. This document describes the frames, not the
> bench harness.

---

## 1. The enable chain (why a single command is not enough)

```
Host (High) 0x111 mode  ─┐
Host (High) 0x112 power ─┤→ RT ── (Low) ──► SYS ── (Low) ──► MTR
Host (High) 0x300 drive ─┘        │                          ▲
                                   └── (Low) 0x204 ──────────┘  (speed/gear)
SYS (Low) 0x011 safety · 0x110 mode · 0x113 power ──────────────► MTR (enable)
```

MTR only enables propulsion when **all** of these are present and fresh:

| Frame | Sender | Meaning |
|---|---|---|
| `0x204` RT_DRIVE_CMD | RT | speed setpoint + gear |
| `0x110` SYS_MODE_CMD | SYS | `mode=1` (AUTO) |
| `0x113` SYS_PWR_CMD | SYS | `power_state=1` (ON) |
| `0x011` SYS_SAFETY_STS | SYS | `estop_active=0` |

So to make the vehicle move you must, in order: **power ON → mode AUTO → drive**.

### 1.1 RT's three READY bits (why 0x204 can carry 0/N even while streaming)

RT always transmits `0x204` at 100 Hz, but it zeroes speed/yaw unless **all three**
READY bits are set (`rt-esp32/src/safety_stream_loss.h`):

| Bit | Granted by | Typical blocker on the bench |
|---|---|---|
| `READY_BIT_HOST` (0x300) | Host drive command, fresh ≤ 500 ms | one-shot `0x300` — it must be a **stream** |
| `READY_BIT_MODE` (0x110) | SYS `0x110` with advancing counter, fresh ≤ 500 ms | SYS absent, or `0x110` clamped to MANUAL by an inhibit (MTR `0x206` / SEB `0x721` missing → check GPIO42 jumper) |
| `READY_BIT_SAFETY` (0x011) | **2 consecutive** CRC-valid `0x011` frames, fresh ≤ 700 ms | SYS not sending `0x011`, or CRC bad (first frame only sets a baseline) |

Diagnostics: `0x501.block_mask` bit0 = no-SYS-authority; RT boot log prints
`DEVELOPER BYPASS ACTIVE` when the GPIO42 jumper was present **at power-on**
(the pin is sampled once at boot — adding the jumper while running does nothing
until reboot).

### 1.2 Bench-solo: driving with SYS disconnected

With `SYSTEM_RUN_MODE=1` + jumper (or `=2`), RT **self-grants** `SAFETY|MODE`
(mode AUTO) once it has never seen a valid SYS `0x011` within 2 s of boot, so
`0x300` → `0x204/0x169/0x205` forwarding keeps working with SYS unplugged —
the same philosophy as SYS broadcasting `0x110/0x113` with MTR/SEB absent.

* A SYS that **was seen and then died** still latches ESTOP (fail-safe preserved).
* The moment a real SYS sends a valid `0x011`, authority hands back to the
  real stream automatically.
* An ESTOP latched while SYS is absent can only be cleared by power-cycle
  (the 2-frame `0x011` clear needs SYS).

---

## 2. Minimal "go" sequence

All Host frames are sent on the **High** bus. Repeat each until its echoed
status confirms it (the first frame of a supervised stream only sets a baseline).

### Step 1 — Power ON
| | |
|---|---|
| Send | `0x112` HMI_PWR_REQ, DLC 2 |
| Bytes | `[0]=req_start=1`, `[1]=rolling_counter++` |
| Rate | ~10–50 Hz until confirmed |
| Confirm | `0x113` SYS_PWR_CMD `power_state=1` (Low, and relayed to High) |

### Step 2 — Mode AUTO
| | |
|---|---|
| Send | `0x111` HMI_MODE_REQ, DLC 2 |
| Bytes | `[0]=req_mode=1`, `[1]=rolling_counter++` |
| Rate | ~10–50 Hz until confirmed |
| Confirm | `0x110` SYS_MODE_CMD `mode=1` (Low) **and** `0x210` RT_STATE_RPT `mode=1` |

### Step 3 — Drive (speed + gear)
| | |
|---|---|
| Send | `0x300` HOST_DRIVE_CMD, DLC 8 |
| Bytes | `[0..3]` `speed_mmps` int32 big-endian (min −500, max 3000) |
| | `[4..6]` `yaw_rate_mrad_s` int24 big-endian signed (steering) |
| | `[7]` `gear` (0=N, 1=D, 2=S, 3=R) |
| Rate | **must be periodic ~50 Hz** — MTR watchdogs `0x204` at 150 ms |
| Confirm | `0x204` RT_DRIVE_CMD on Low carries the speed/gear (→ MTR) |

Example frames (big-endian):

| speed | gear | `0x300` data |
|---|---|---|
| 1000 mm/s, D | 1 | `000003E8 000000 01` |
| −500 mm/s, R | 3 | `FFFFFE0C 000000 03` |
| 0 mm/s, N | 0 | `00000000 000000 00` |

`0x204` produced by RT (DLC 5): `[0..3]=speed int32`, `[4]=gear`.
Dropping the `0x300` stream stops motion (setpoint → 0) without latching ESTOP.

### Step 4 — Steering (optional, two ways)
* **Yaw coupling:** set `yaw_rate_mrad_s` in `0x300` (`0x169` follows). Range ±3000.
* **Direct angle:** `0x303` HOST_STEER_CMD, DLC 4, **periodic ~100 Hz**
  (goes stale after 100 ms):
  `[0..1]=steer_angle_0_1deg int16 BE` (−450…450 = −45.0…45.0°),
  `[2] bit0=angle_valid`, `[3]=rolling_counter`.

---

## 3. Braking

| | |
|---|---|
| Send | `0x301` HOST_BRAKE_REQ, DLC 4 — `brake_pressure_kpa` int32 BE (0…20000) |
| Path | RT clamps to `max(obstacle, host)` capped at **5000 kPa** → `0x205` (Low) |
| Then | SYS converts `0x205` → `0x7B9` VCU_SEB_REQ (Low): **Pressure mode**, `pressure_raw = (kPa+25)/50` |
| Release | send `0` → SYS returns to **Stroke mode**, `stroke_raw=600` (0 mm) |

| kPa | `0x7B9` pressure_raw |
|---|---|
| 1000 | 20 |
| 2000 | 40 |
| 3000 | 60 |
| 5000 (max) | 100 |

## 4. Obstacle → automatic brake

| | |
|---|---|
| Send | `0x400` HOST_OBSTACLE_DIST, DLC 4 — `distance_mm` uint32 BE |
| Clear | `distance_mm = 0xFFFFFFFF` |
| Effect | RT cuts `0x204` speed and raises its `0x205` brake intent; SYS relays to `0x7B9`. `distance ≤ 300 mm` ⇒ 5000 kPa; `≥ 3000 mm` ⇒ 0 |

## 5. Lights

| | |
|---|---|
| Send | `0x302` HOST_LIGHT_CMD, DLC 1 (`[0]` bits: 0=left, 1=right, 2=brake, 3=headlight) |
| Path | forwarded High→Low by RT; **SYS** drives the lamps and reports them in `0x011` |
| Confirm | `0x011` `light_left/right/brake/head`. Turn lamps blink at 2 Hz; brake lamp is forced on in ESTOP |

## 6. ESTOP and reset

EMERGENCY STOP:
| | |
|---|---|
| Send | `0x001` SAFETY_ESTOP, DLC 0 (on High or Low; any sender) |
| Effect | RT: `0x204`→0/gear N, steering ramps→silent. SYS: `0x7B9` = max stroke **1140**, brake lamp on. Both nodes latch. |

Recovery (staged — the first frame only establishes a baseline):
| | |
|---|---|
| Send | `0x114` HOST_ESTOP_RESET_REQ, DLC 4, **2+ counter-advancing frames** |
| Bytes | `[0]=request_seq` (advance), `[1..2]=reset_token=0x5253`, `[3]=rolling_counter` |
| Confirm | `0x115` SYS_ESTOP_RESET_RSP `result=0 (ACCEPTED)`, `blocker_mask=0` |
| Blockers | `0x01` physical e-stop (GPIO1) open, `0x10` MTR ack, `0x40` invalid/freshness |

> The physical e-stop loop must be **closed** (SYS GPIO1 pulled to GND) or the
> reset is rejected with blocker `0x01`.

---

## 7. Frame summary

| ID | Name | Bus | DLC | Rate | Dir |
|---|---|---|---|---|---|
| `0x111` | HMI_MODE_REQ | High | 2 | on demand | Host→RT |
| `0x112` | HMI_PWR_REQ | High | 2 | on demand | Host→RT |
| `0x300` | HOST_DRIVE_CMD | High | 8 | 50 Hz | Host→RT |
| `0x301` | HOST_BRAKE_REQ | High | 4 | on change | Host→RT |
| `0x302` | HOST_LIGHT_CMD | High | 1 | on change | Host→RT/SYS |
| `0x303` | HOST_STEER_CMD | High | 4 | 100 Hz | Host→RT |
| `0x400` | HOST_OBSTACLE_DIST | High | 4 | on change | Host→RT |
| `0x114` | HOST_ESTOP_RESET_REQ | High | 4 | 2+ frames | Host→SYS |
| `0x001` | SAFETY_ESTOP | both | 0 | on demand | any→all |
| `0x204` | RT_DRIVE_CMD | Low | 5 | 100 Hz | RT→MTR/SYS |
| `0x205` | RT_BRAKE_CMD | Low | 4 | 50 Hz | RT→SYS |
| `0x169` | VCU_SES_REQ | Low | 8 | 50 Hz | RT→SES |
| `0x7B9` | VCU_SEB_REQ | Low | 8 | 50 Hz | SYS→SEB |
| `0x011` | SYS_SAFETY_STS | Low(+High) | 5 | 5 Hz | SYS |
| `0x110` | SYS_MODE_CMD | Low | 2 | 10 Hz | SYS→MTR/RT |
| `0x113` | SYS_PWR_CMD | Low | 2 | 10 Hz | SYS→MTR |
| `0x210` | RT_STATE_RPT | High(+Low) | 6 | 10 Hz | RT |
| `0x501` | RT_NODE_STATUS | Low(+High) | 8 | 50 Hz | RT |
| `0x115` | SYS_ESTOP_RESET_RSP | Low(+High) | 5 | on reset | SYS→RT |

---

## 8. Measured latency (500 kbit/s bench, medians)

| Path | Median |
|---|---|
| High→Low relay (`0x112`/`0x302`) | ~2.5–3.3 ms |
| Low→High relay (`0x011`/`0x500`) | ~3.1–3.5 ms |
| `0x300` speed → `0x204` (MTR) | ~10 ms |
| `0x300` yaw → `0x169` (SES) | ~16 ms |
| `0x301` brake → `0x205` (SYS) | ~21 ms |
| `0x301` brake → `0x7B9` (SEB) | ~39 ms |
| `0x302` headlight → `0x011` lamp (SYS tick) | ~120 ms |

Gateway relay loss at ~100 Hz is 0 % (measured). Numbers vary with bus load.

---

## 9. Bench prerequisites (not needed on the real vehicle)

* `SYSTEM_RUN_MODE=1` bench firmware requires the **developer-override jumper
  GPIO42 → GND** on **both SYS and RT**, fitted **before power-on**; otherwise
  SYS clamps `0x110`→MANUAL (`0x113`→OFF) and RT zeroes every drive command.
  Boot log shows `DEVELOPER BYPASS ACTIVE` on both nodes.
* With the jumper active, SYS tolerates missing MTR/SEB, and RT keeps
  forwarding Host commands even with SYS unplugged (see §1.2).
* The physical e-stop loop (SYS GPIO1 → GND when healthy) must be closed.
* Sender-side: the control-toolkit backend can emit all of the above (see
  `control-toolkit/backend/tests/hw_bench/harness.py`, `start_drive`,
  `send_brake`, `send_lights`, `start_steer`, `trip_estop`, `reset_estop`).
