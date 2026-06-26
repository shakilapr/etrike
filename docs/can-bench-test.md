# RT ↔ SYS CAN Bus Bench Test Plan

**Goal:** Validate CAN communication between RT-ESP32 and SYS-ESP32 on the
low-level CAN bus, using CANalyst-II to monitor traffic and inject signals that
mimic absent third-party hardware (MTR, EPS-C, SEB, Host). No motor, steering
rack, brake actuator, or high-voltage power required.

---

## 1. Architecture Under Test

```
┌──────────────────┐         ┌──────────────────┐
│   RT ESP32-S3    │         │   SYS ESP32-S3   │
│                  │         │                  │
│  TWAI (low bus)  │         │  TWAI (low bus)  │
│  TX=5  RX=4      │         │  TX=5  RX=4      │
│       │          │         │       │          │
│   SN65HVD230     │         │   SN65HVD230     │
│       │          │         │       │          │
│   CAN_H ────────[shared low CAN bus, 500 kbit/s]───────┐
│   CAN_L ────────────────────────────────────────────────┤
│                  │         │                  │         │
│  MCP2515 (high)  │         │                  │    CANalyst-II
│  UNCONNECTED     │         │                  │    (monitor + inject)
└──────────────────┘         └──────────────────┘         │
                                                   ┌──────┴──────┐
                                                   │ CANalyst-II │
                                                   │ Ch0 → low   │
                                                   │ Ch1 → unused│
                                                   └──────┬──────┘
                                                          │ USB
                                                   ┌──────┴──────┐
                                                   │ Debug Tool  │
                                                   │ Backend :3000│
                                                   │ UI :5173     │
                                                   └─────────────┘
```

- **RT's high bus (MCP2515) is disconnected** — no Host (Jetson) commands arrive.
  RT boots fine without it; `Mcp2515Driver::init()` fails gracefully (return value
  unchecked in `app_main`), and the high-bus tasks spin harmlessly.
- **MTR, EPS-C, SEB are absent** — we inject their frames via CANalyst-II.
- **SYS pulls up** ESTOP, brake lever, mode, and start button GPIOs internally
  (all are NC active-low with pull-up), so they read as "not pressed." If any
  float low, define `TESTING` in the SYS build to skip GPIO reads.

---

## 2. Prerequisites

### 2.1 Hardware

| Item | Qty | Notes |
|------|-----|-------|
| RT ESP32-S3 board | 1 | With SN65HVD230 CAN module attached (TWAI TX=5, RX=4) |
| SYS ESP32-S3 board | 1 | With SN65HVD230 CAN module attached (TWAI TX=5, RX=4) |
| CANalyst-II USB analyzer | 1 | Zadig WinUSB driver bound to both channels |
| 120Ω termination | ≥1 | Usually built into the SN65HVD230 modules — verify one is present |
| Jumper wires | 3 | CAN-H, CAN-L, GND between the two SN65HVD230 modules |
| USB cables | 3 | Power + serial for RT, SYS, and CANalyst-II |

### 2.2 Wiring

```
RT SN65HVD230          SYS SN65HVD230         CANalyst-II
┌──────────┐          ┌──────────┐          ┌──────────┐
│ CAN_H ●──┼──────────┼──● CAN_H │          │          │
│ CAN_L ●──┼──────────┼──● CAN_L │          │          │
│ GND   ●──┼──────────┼──● GND   │          │          │
└──────────┘          └──────────┘          │          │
       │                                     │          │
       └─────────────────────────────────────┤ CAN_H ●──┤
                                             │ CAN_L ●──┤
                                             │ GND   ●──┤
                                             └──────────┘
```

- CAN-H (often yellow/green wire on SN65HVD230) → CAN-H on both modules and CANalyst-II Ch0 H
- CAN-L (often white/brown wire on SN65HVD230) → CAN-L on both modules and CANalyst-II Ch0 L
- GND → common ground between all three
- **Verify at least one 120Ω resistor** across CAN-H/CAN-L — most SN65HVD230 modules
  have a jumper or solder bridge for this. Measure ~60Ω between CAN-H and CAN-L
  when both modules are connected (two 120Ω in parallel).

### 2.3 Software

```powershell
# Install dependencies (one-time)
cd debug-tool\backend && npm install
cd debug-tool\ui && npm install

# CANalyst-II: Download Zadig from https://zadig.akeo.ie/
#  1. Plug in CANalyst-II
#  2. Run Zadig as Administrator
#  3. Options → List All Devices
#  4. Select "CANalyst-II" (or "STM32 Virtual ComPort") → WinUSB driver → Replace Driver
```

### 2.4 Firmware

Both RT and SYS firmware work **as-is — no code changes required.** Flash the
current `main` branch:

```powershell
cd rt-esp32  && pio run -t upload
cd sys-esp32 && pio run -t upload
```

Verify each board boots via serial monitor (115200 baud):
- RT prints: `RT ESP32-S3 boot` … `Ready — 8 tasks`
- SYS prints: `SYS ESP32-S3 initializing...` … `Ready — 15 tasks running. Mode=MANUAL`

> **Note:** RT may log `MCP2515 not in config mode after reset` — this is expected;
> the MCP2515 (high bus) is not installed. RT continues booting normally.

---

## 3. Start the Debug Tool

### 3.1 Backend (CANalyst-II mode)

```powershell
cd debug-tool\backend
$env:CAN_TRANSPORT = "canalystii"
$env:CANALYST_BITRATE = "500000"
$env:CANALYST_CH0_BUS = "low"     # we're on the low bus only
$env:CANALYST_CH1_BUS = "low"     # unused, set to low to avoid confusion
npm run dev
```

Expected output:
```
CANalyst-II bridge: connected (device 0)
  ch0 → low bus  (500000 bit/s)
  ch1 → low bus  (500000 bit/s)
Server listening on http://127.0.0.1:3000
```

### 3.2 UI

```powershell
cd debug-tool\ui
npm run dev
```

Open **http://localhost:5173** in a browser.

---

## 4. What Happens Naturally (No Injection)

With both boards powered and CAN wired, you will see these frames immediately
in the **CAN Monitor → Low Bus** tab:

| CAN ID | Name | Sender | Period | Expected Content |
|--------|------|--------|--------|-----------------|
| `0x7FD` | RT_HEARTBEAT | RT | 2 Hz | `alive_ctr` incrementing |
| `0x7FE` | SYS_HEARTBEAT | SYS | 10 Hz | `alive_ctr` incrementing |
| `0x011` | SYS_SAFETY_STS | SYS | 5 Hz | `estop_active=0, heartbeat_ok=1` |
| `0x110` | SYS_MODE_CMD | SYS | on change | `mode=0` (MANUAL) |
| `0x204` | RT_DRIVE_CMD | RT | 100 Hz | `motor_speed_mmps=0, gear=N` |
| `0x600` | SYS_DIAG_RPT | SYS | 1 Hz | `mode=0, estop=0, hb_ok=1` |

**Verify these baseline checks:**

| Check | How to verify |
|-------|---------------|
| RT heartbeat OK | Dashboard → "HB RT: ✅" |
| SYS heartbeat OK | `0x011` decoded `heartbeat_ok=1` |
| No ESTOP | `0x011` decoded `estop_active=0` |
| Mode = MANUAL | `0x110` decoded `mode=0` or `mode_name=MANUAL` |
| RT sending drive | `0x204` appears at ~100 Hz, speed=0, gear=N |

If `0x011` shows `heartbeat_ok=0` or `estop_active=1`, check wiring (section 9).

---

## 5. Mimic Absent Nodes — Injection Guide

The bus has four missing nodes. Inject each to keep RT and SYS fully satisfied.

### 5.1 Quick-Start: Inject All at Once

Save as `start-bench-injections.ps1`:

```powershell
# start-bench-injections.ps1
# Mimics MTR + EPS-C + SEB on the low CAN bus for RT↔SYS bench testing.
# Requires debug-tool backend running on :3000 with CANalyst-II.

param(
  [string]$Backend = "http://localhost:3000"
)

$headers = @{ "Content-Type" = "application/json" }

$periodics = @(
  # ── MTR (motor controller) ──────────────────────────────────────
  @{ id="0x120"; dlc=2; data=@(0,0);           ms=10; desc="MTR throttle status (0 mm/s)" },
  @{ id="0x206"; dlc=4; data=@(0,0,0,0);       ms=20; desc="MTR motor feedback (speed=0, N, no faults)" },

  # ── EPS-C (steering actuator) ────────────────────────────────────
  @{ id="0x201"; dlc=8; data=@(1,0,0,0,0,0,0,0); ms=10; desc="EPS-C status (angle=0, OK)" },

  # ── SEB (brake actuator) ─────────────────────────────────────────
  @{ id="0x721"; dlc=8; data=@(1,0,0,0,0,0,0,0); ms=10; desc="SEB status (stroke=0, OK)" },

  # ── RT drive keep-alive (in case RT's own 0x204 stops) ──────────
  @{ id="0x204"; dlc=5; data=@(0,0,0,0,0);     ms=10; desc="RT drive cmd (0 mm/s, N) — redundant with RT's own" }
)

Write-Host "Starting periodic CAN injections on low bus..." -ForegroundColor Cyan
foreach ($inj in $periodics) {
  $body = @{
    action      = "start"
    bus         = "low"
    id          = $inj.id
    dlc         = $inj.dlc
    data        = $inj.data
    interval_ms = $inj.ms
  } | ConvertTo-Json

  try {
    $null = Invoke-RestMethod -Uri "$Backend/api/cmd/periodic" -Method Post -Headers $headers -Body $body
    Write-Host "  OK  $($inj.desc) — $($inj.id) @ $($inj.ms)ms" -ForegroundColor Green
  } catch {
    Write-Host "  FAIL $($inj.desc) — $($_.Exception.Message)" -ForegroundColor Red
  }
}

Write-Host ""
Write-Host "All injections running. Open http://localhost:5173 to monitor." -ForegroundColor Cyan
Write-Host "Stop injections: restart the backend, or call POST /api/cmd/periodic with action=stop for each ID."
```

Run it:
```powershell
cd E:\doc\etrike
.\start-bench-injections.ps1
```

### 5.2 Inject Individually (via REST)

Use these for manual testing or scripting:

```powershell
$backend = "http://localhost:3000"
$headers = @{ "Content-Type" = "application/json" }

# MTR throttle status — 0 mm/s, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x120"; dlc=2; data=@(0,0); interval_ms=10
} | ConvertTo-Json)

# MTR motor feedback — 0 mm/s, gear N, no faults, 50 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x206"; dlc=4; data=@(0,0,0,0); interval_ms=20
} | ConvertTo-Json)

# EPS-C status — angle 0°, OK, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x201"; dlc=8; data=@(1,0,0,0,0,0,0,0); interval_ms=10
} | ConvertTo-Json)

# SEB status — stroke 0, OK, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x721"; dlc=8; data=@(1,0,0,0,0,0,0,0); interval_ms=10
} | ConvertTo-Json)

# Stop a specific injection
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="stop"; bus="low"; id="0x201"
} | ConvertTo-Json)
```

### 5.3 Inject via UI

The **Injector** tab offers a form-based interface:
1. Select **Low Bus**
2. Pick the CAN ID from the dropdown (filtered to low-bus IDs only)
3. Fill in decoded field values
4. Click **▶ Send Periodic** for repeating injection, or **Send Once** for a single frame

The keyboard shortcuts also work on the low bus:
| Key | Low Bus Action |
|-----|---------------|
| `W` / `S` | `0x204` speed ±200 |
| `A` / `D` | `0x169` angle ±5° |
| `Space` ×2 | `0x001` ESTOP |
| `B` / `R` | `0x205` brake kPa set/release |
| `Esc` | Zero `0x204` + `0x205` + `0x169` |

---

## 6. Test Scenarios

### 6.1 Scenario A — Heartbeat Exchange (Passive)

**Goal:** Confirm basic CAN connectivity.

1. Start backend + UI (section 3). Do NOT run injections yet.
2. Power RT and SYS.
3. In the UI's Low Bus tab, verify `0x7FD` (RT→SYS) and `0x7FE` (SYS→RT)
   appear. Both counters should increment.

**Pass:** `heartbeat_ok=1` in `0x011`, no ESTOP, RT heartbeat alive counter
changes each frame.

### 6.2 Scenario B — Inject Drive Command

**Goal:** Verify SYS receives and processes injected drive commands.

1. Inject `0x204` with speed=1500 mm/s:
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x204"; dlc=5; data=@(0,0,5,0xDC,1)
   } | ConvertTo-Json)
   ```
   (data bytes: speed_mmps=1500 [0x05DC big-endian], gear=1 [D])

2. SYS dispatch task updates `g_setpoint_speed_mmps` → 1500.
3. SYS motor task outputs 1500 mm/s on DAC (MCP4725 at I2C 0x60).

**Pass:** SYS logs no errors. If an oscilloscope is on the MCP4725 output pin,
voltage changes proportional to 1500/3000 × Vref.

### 6.3 Scenario C — ESTOP via CAN

**Goal:** Verify ESTOP propagation and both nodes' reactions.

1. Inject ESTOP (requires `confirm_estop: true`):
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x001"; dlc=0; data=@(); confirm_estop=$true
   } | ConvertTo-Json)
   ```

2. Observe:
   - SYS `0x011` → `estop_active=1`
   - SYS `0x110` → mode changes to ESTOP (mode=2)
   - RT's `t_dispatch` → queues `SafetyEvent::ESTOP` → control loop zeroes setpoints
   - RT sends `0x001` on both buses (low bus via TWAI, high bus via MCP2515 — the
     latter fails silently since MCP2515 is disconnected)
   - SYS indicator bulbs switch (AUTO → off, MANUAL → off in ESTOP)
   - SYS 12V power relay opens (`kPower12vRelay` → off)
   - SYS DAC output → 0V

**Pass:** Both nodes enter ESTOP. `0x011` shows `estop_active=1`. RT `0x204`
drops to speed=0, gear=N.

### 6.4 Scenario D — RT Heartbeat Loss → SYS ESTOP

**Goal:** Verify SYS detects RT heartbeat timeout and enters ESTOP.

1. Stop the RT board (disconnect USB or press RST and hold).
2. Watch SYS behavior:
   - After 1000ms (`kHeartbeatTimeoutMsRt`): `0x011` → `heartbeat_ok=0`
   - SYS safety task: `estop_triggered = true` (heartbeat not OK)
   - SYS enters ESTOP, sends CAN `0x001`, broadcasts `0x011` with `estop_active=1`
   - SYS DAC → 0V, 12V relay opens
3. Reconnect RT.
4. After RT resumes sending `0x7FD`: SYS `heartbeat_ok` returns to 1.
   ESTOP must be manually cleared (START button or mode long-press per gap #11).

**Pass:** SYS detects heartbeat loss within ~1s. SYS enters ESTOP.

### 6.5 Scenario E — Mode Change (MANUAL ↔ AUTO)

**Goal:** Verify mode transitions work over CAN.

1. Inject AUTO mode:
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x110"; dlc=1; data=@(1)
   } | ConvertTo-Json)
   ```

2. Observe: `0x110` shows mode=1 (AUTO). SYS motor task switches from
   ADC pass-through to CAN setpoint mode. SYS indicator shows AUTO bulb on.

3. Inject MANUAL mode:
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x110"; dlc=1; data=@(0)
   } | ConvertTo-Json)
   ```

**Pass:** Both transitions complete without ESTOP. SYS `0x110` reflects the
change. Indicator bulbs follow mode.

### 6.6 Scenario F — SYS 0x204 Staleness

**Goal:** Verify that when `0x204` stops arriving, SYS zeros the speed setpoint
(but does NOT ESTOP — staleness zeroes, doesn't ESTOP).

1. Stop the periodic `0x204` injection (and stop RT if it's also sending 0x204):
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
     action="stop"; bus="low"; id="0x204"
   } | ConvertTo-Json)
   ```
   (If RT is still sending its own 0x204, this test won't work — you'd need to
   modify RT or stop both sources.)

2. After 200ms (`kSetpointStaleMs`): SYS sets `g_setpoint_speed_mmps=0`,
   `g_setpoint_gear=N`.

3. Restart `0x204` injection — speed setpoint recovers on next frame.

**Pass:** SYS logs no ESTOP. SYS zeros speed internally. SYS recovers when
`0x204` resumes.

---

## 7. What Each Injection Mimics

| Real Node | CAN IDs It Sends | What to Inject | Why |
|-----------|-----------------|----------------|-----|
| **MTR (STM32)** | `0x120` (100 Hz), `0x206` (50 Hz) | Both | SYS EGAS L2 checks `0x206` actual vs `0x204` cmd in AUTO mode. RT forwards `0x120` to Host. Missing → EGAS may false-trigger. |
| **EPS-C (steering)** | `0x201` (100 Hz), `0x202` (10 Hz), `0x203` (1 Hz), `0x6FA` (100 Hz) | `0x201` minimum | RT steering state machine needs `0x201` to track angle. Without it, steering stays in LISTEN_SYNC, times out after 5s → FAULT. |
| **SEB (brake)** | `0x721` (100 Hz), `0x731` (10 Hz), `0x741` (1 Hz), `0x6FB` (100 Hz) | `0x721` minimum | SYS brake task checks `0x721` staleness (100ms). Without it, SYS logs warnings. Brake following-error monitor needs actual stroke. |
| **Host (Jetson)** | `0x300`, `0x301`, `0x302`, `0x400`, `0x7FC` (high bus) | Not needed | RT's high bus (MCP2515) is disconnected. RT control loop runs with `cmd={0,0}`. SYS doesn't see high-bus frames directly. |

**Minimum injection set** for a quiet bench: `0x201` (keep RT steering happy),
`0x721` (keep SYS brake happy), `0x120` + `0x206` (keep EGAS L2 happy).

If you skip all injections, RT and SYS still exchange heartbeats and basic status
frames. SYS stays in MANUAL mode. RT steering stays in BOOT_WAIT/LISTEN_SYNC.
This is fine for basic connectivity testing.

---

## 8. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No frames in UI | CANalyst-II not connected | Check USB, Zadig driver, `CAN_TRANSPORT=canalystii` |
| Only one board's frames visible | CAN wiring open/short | Check CAN-H/CAN-L continuity. Verify 120Ω termination. |
| SYS stuck in ESTOP | RT heartbeat missing (`0x7FD`) | Check RT is powered, CAN wired correctly, TWAI TX/RX not swapped |
| `heartbeat_ok=0` in `0x011` | RT `0x7FD` not arriving at SYS | SYS TWAI RX issue or CAN bus wiring |
| RT steering FAULT | No `0x201` (EPS-C status) injected | Start `0x201` periodic injection (section 5) |
| SYS CAN bus-off errors | No termination resistor | Add 120Ω across CAN-H/CAN-L on one SN65HVD230 |
| CANalyst-II `connect timeout` | Wrong device or driver | Re-run Zadig, check Device Manager for "WinUSB" device |
| `Cmd too long` or API errors | Backend not running | Start backend first (`npm run dev` in `debug-tool/backend`) |
| RT serial: `MCP2515 not in config mode` | Normal — MCP2515 not installed | Expected. RT continues booting. No action needed. |

### Quick CAN Bus Health Check

Measure with a multimeter (power off):
- **CAN-H to CAN-L:** ~60Ω (two 120Ω in parallel) if both modules have termination
- **CAN-H to GND:** >1kΩ (should not be shorted)
- **CAN-L to GND:** >1kΩ (should not be shorted)
- **CAN-H to VCC (3.3V):** >1kΩ

With power on and bus idle:
- **CAN-H:** ~2.5V (recessive)
- **CAN-L:** ~2.5V (recessive)
- During active traffic, CAN-H swings to ~3.5V, CAN-L to ~1.5V (dominant).

---

## 9. Code Changes Required

**None.** Both RT and SYS firmware work as-is for this bench test.

What happens for each subsystem with the partial bus:

| Subsystem | Behavior |
|-----------|----------|
| RT `Mcp2515Driver::init()` | Returns `false` — no MCP2515 on SPI. `app_main` does not check the return value. RT boots normally. |
| RT high-bus RX/TX tasks | Spin harmlessly — `send()`/`receive()` return `false` immediately when `m_initialized=false`. |
| RT control loop | Runs at 100 Hz. No Host commands arrive → `cmd={0,0}`. Physics resolves to zero speed/steer. Setpoint published to `g_setpoint_q`. |
| RT `t_can_tx_low` | Sends `0x204` at 100 Hz with `{speed=0, gear=N}`. Steering state machine gates drive (requires ACTIVE/ESTOP states). Sends `0x205` only in non-MANUAL mode. Sends `0x169` only in non-MANUAL mode. Sends `0x7FD` at 2 Hz. |
| RT forwarding | Low→high forwards go to MCP2515 (fail silently). High→low receives nothing (no high bus). No effect on the bench test. |
| SYS `task_safety` | GPIOs pulled up → ESTOP button and brake lever read as "not pressed." `heartbeat_ok()` checks RT `0x7FD`. EGAS L2 only runs in AUTO mode. |
| SYS `task_motor` | In MANUAL mode: reads physical throttle ADC → DAC. In AUTO mode: uses CAN `0x204` setpoint with staleness check. |
| SYS `task_brake` | Sends `0x7B9` to SEB in MANUAL/ESTOP (suppressed in AUTO per gap #12). If `0x721` missing >100ms, logs staleness warning (no ESTOP). |
| SYS mode manager | Starts in MANUAL. Listens for button presses (none pressed due to pull-ups). Transitions on CAN `0x110` or `0x001`. |

### Optional Firmware Tweaks

If you want RT to generate non-zero drive commands without a Host, add this
to `rt-esp32/src/main.cpp` in the control task, after `cmd = {0, 0}`:

```cpp
// BENCH TEST: generate a small forward speed without Host
// Remove before connecting to real hardware!
if (xQueueReceive(g_cmd_q, &cmd, 0) != pdTRUE) {
    cmd = {500, 0, uint8_t(can::Gear::D)};  // 0.5 m/s forward, D gear
}
```

This makes RT send `0x204{speed=500, gear=D}` at 100 Hz, which SYS will use in
AUTO mode to output a non-zero DAC voltage. **Do not commit this change.**

---

## 10. Shutdown Procedure

1. Stop periodic injections (or just stop the backend — all injections auto-cancel):
   ```powershell
   # Stop each injection
   foreach ($id in @("0x120","0x206","0x201","0x721","0x204")) {
     Invoke-RestMethod -Uri "http://localhost:3000/api/cmd/periodic" -Method Post `
       -Headers @{"Content-Type"="application/json"} `
       -Body (@{action="stop"; bus="low"; id=$id} | ConvertTo-Json) 2>$null
   }
   ```
2. Ctrl+C in the backend terminal.
3. Ctrl+C in the UI terminal.
4. Disconnect USB power from RT and SYS.

---

## 11. References

- [Architecture Overview](../architecture.md) — system topology and message catalog
- [CAN Protocol](../shared/can/can_protocol.h) — message ID constants and struct layouts
- [Debug Tool Architecture](../debug-tool/debug-tool-architecture.md) — tool design and API
- [RT Config](../rt-esp32/src/config.h) — RT timing and GPIO constants
- [SYS Config](../sys-esp32/src/config.h) — SYS timing and GPIO constants
