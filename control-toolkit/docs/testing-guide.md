# Control Toolkit — software-only testing guide

How to validate the eTrike **control and command path** on this PC **without CANalyst, without flashing boards, and without a full multi-ECU vehicle simulation**.

Cross-checked against:

| Source of truth | Location |
|-----------------|----------|
| Wire contracts | `protocol/contracts/*.yaml` (`host`, `rt`, `mtr`, `ses`, `seb`, `sys`, `hmi`, `network`) |
| Shared limits | `shared/shared_config.h` |
| Host kinematics in toolkit | `control-toolkit/backend/.../control_intent.py` |
| RT command chain | `rt-esp32/src/main.cpp`, `direct_resolver.*`, `steering_control.h`, `seb_request.h` |
| MTR command RX | `mtr-stm32/src/main.cpp` (listens **0x204**) |
| SYS brake → SEB | `sys-esp32/src/brake_control.h` (TX **0x7B9** from lever / ESTOP / **0x205**) |
| Codecs | `protocol/codecs` + `protocol_bridge` (toolkit never invents wire layouts) |

Related docs: [`../run.md`](../run.md) · [`api-dictionary.md`](api-dictionary.md) · [`computer-mode-frameworks.md`](computer-mode-frameworks.md) · repo [`docs/testing-guide.md`](../../docs/testing-guide.md).

---

## 1. What software-only is (and is not)

| Layer | Computer / `pure_software` | Real vehicle |
|-------|----------------------------|--------------|
| Protocol YAML + encode/decode | **Same package** (`protocol/`) | Same |
| Toolkit TX gate, scheduler, sessions | Running in Python | Same API against physical adapter |
| Dual CAN High / Low | **Virtual** (`python-can`), in-process | CANalyst or vehicle wiring @ 500 kbit/s |
| **RT / SYS / MTR / SES / SEB firmware** | **Not executed** | Flashed ECUs |
| RT kinematics + gateway | **Not executed** | RT owns translation High → Low |

**Design (from toolkit + architecture comments):** Pure Software is a **command-path and protocol lab**. Virtual TX is looped back as RX in the same process, so Live CAN shows **what left the toolkit encoder**. You do **not** need actuator feedback to sign off “right frame on the wire.”

Topology nodes (Host / RT_high / RT_low / SYS / MTR heartbeats) stay **offline** unless something actually transmits those IDs — expected here.

---

## 2. Real vehicle chain (original firmware) vs toolkit paths

### 2.1 What the vehicle does when Host says “drive”

From **protocol YAML + `rt-esp32` + `sys-esp32` + `mtr-stm32`**:

```
Host (Jetson / toolkit High)
  │  High 0x300 HOST_DRIVE_CMD   cycle 10 ms
  │  fields: speed_mmps, yaw_rate_mrad_s, gear {N,D,S,R}
  ▼
RT  (rt-esp32)
  │  resolve kinematics (physics_model or direct_resolver)
  │    speed_mmps  → motor_speed_mmps (clamp ± shared limits)
  │    yaw_rate    → steer_angle_mdeg (direct: ×15 mdeg per mrad/s)
  │
  ├─ Low 0x204 RT_DRIVE_CMD     10 ms → receivers SYS, MTR
  │     motor_speed_mmps, gear   (DLC 5)
  │     gated: AUTO (not MANUAL); steer state ready; else {0,N}
  │
  ├─ Low 0x169 VCU_SES_REQ      20 ms → receiver EPS_C (SES / steer-by-wire)
  │     vendor opaque; RT steering_control builds angle + enables
  │
  ├─ Low 0x205 RT_BRAKE_CMD     20 ms → receiver SYS
  │     brake_pressure_kpa
  │
  └─ Low 0x7B9 VCU_SEB_REQ      20 ms → SEB  (RT only in AUTO when steer ACTIVE,
                                              or SYS heartbeat takeover)
        primary authority on vehicle is usually SYS (see below)

SYS (sys-esp32) — brake-by-wire path to SEB
  │  consumes 0x205 (+ lever, ESTOP, SEB 0x721 status)
  └─ Low 0x7B9 VCU_SEB_REQ      50 Hz → SEB
        alignment_enable, control_enable, control_mode, pressure/stroke raw, …

MTR (mtr-stm32) — motor unit
  │  RX 0x204 RT_DRIVE_CMD → g_cmd_speed_mmps, g_cmd_gear
  │  RX 0x001 SAFETY_ESTOP, 0x110 SYS_MODE_CMD
  └─ TX 0x120 SYS_THROTTLE_STS, 0x206 MTR_MOTOR_FBK  (feedback — not required for cmd tests)

SES / EPS_C — steer-by-wire unit (vendor)
  RX 0x169 · TX 0x201 SES_STATUS, 0x202, 0x203, …

SEB — brake-by-wire unit (vendor)
  RX 0x7B9 · TX 0x721 SEB_STATUS, 0x731, 0x741, …
```

**Network gateway note (`network.yaml`):** RT bridges selected frames High↔Low (ESTOP, lights, HMI mode/power, MTR fbk, …). **`HOST_DRIVE_CMD` is not a same_frame bridge** — RT must **regenerate** Low actuator frames. The toolkit does **not** implement that RT task in Computer mode.

### 2.2 What the toolkit actually emits (two exclusive methods)

From `ControlIntentService` (`control_intent.py`): High kinematics and Low direct are **mutually exclusive**.

| Toolkit method | Emulates which ECU role | Frames you will see | Does **not** run |
|----------------|-------------------------|---------------------|------------------|
| **High** (Drive, keyboard, `/control/intent`, `/analysis/host-drive`) | **Host** only | **0x300** `HOST_DRIVE_CMD` on **high** @ ~10 ms | RT resolve → 0x204 / 0x169 / 0x205 / 0x7B9 |
| **Low direct** (Control Low cards, `/control/direct`) | **Wire outputs** of RT (motor/steer) and SYS/RT (SEB cmd) | **0x204**, **0x169**, **0x7B9** on **low** | RT state machines, SYS boot/sync, SES/SEB hardware |

Low direct comment in code: *“Bypass RT kinematics; control_enable + alignment forced ON”* — intentional **unit-command** path so you can observe the same IDs MTR / SES / SEB would receive **without** RT/SYS firmware.

```
                    ┌── High method ──► virtual High 0x300  (Host role)
 Toolkit  ─────────┤
                    └── Low method  ──► virtual Low  0x204 / 0x169 / 0x7B9
                                        (same IDs as RT/SYS would TX)
```

**Implication for “10 s high-level drive and see MTR / SES / SEB frames”:**

- On the **real vehicle**, one Host command becomes all Low unit commands **because RT (+ SYS) run**.
- In **software-only toolkit**, you must either:
  1. **Observe Host command only** (High method → 0x300), *or*
  2. **Drive the unit command frames yourself** (Low method → 0x204 / 0x169 / 0x7B9) and check they match what the original firmware is specified to TX.

There is **no** in-process “RT digital twin” that auto-expands 0x300 into Low frames.

---

## 3. Frame / ECU cheat sheet (contract-accurate)

### 3.1 Commands to observe (TX from toolkit or, on vehicle, from RT/SYS)

| ID | Name | Bus | DLC | cycle | Contract sender → receivers | Toolkit Low channel | Firmware that **TX** this on vehicle |
|----|------|-----|-----|-------|----------------------------|---------------------|--------------------------------------|
| **0x300** | `HOST_DRIVE_CMD` | high | 8 | 10 ms | Host → RT | *(High method only)* | Host / toolkit High |
| **0x204** | `RT_DRIVE_CMD` | low | **5** | 10 ms | RT → **SYS, MTR** | `motor` | **rt-esp32** `t_can_tx_low` |
| **0x169** | `VCU_SES_REQ` | low | 8 | 20 ms | RT → **EPS_C** | `steering` | **rt-esp32** `steering_control` + SES encode |
| **0x205** | `RT_BRAKE_CMD` | low | 4 | 20 ms | RT → **SYS** | *(not a Low direct channel)* | **rt-esp32** |
| **0x7B9** | `VCU_SEB_REQ` | low | 8 | 20 ms | **SYS** → SEB (YAML); RT also in AUTO/takeover | `brake` | **sys-esp32** `BrakeControl` primarily; RT secondary |

### 3.2 Feedback (not required for software-only command tests)

| ID | Name | Bus | Producer | Notes |
|----|------|-----|----------|-------|
| 0x206 | `MTR_MOTOR_FBK` | low (+ high same_frame) | MTR | `actual_speed_mmps`, `gear_state`, `fault_flags` |
| 0x120 | `SYS_THROTTLE_STS` | low (+ high) | MTR | speed report |
| 0x201 | `SES_STATUS` | low | EPS_C | steer status; RT uses for sync |
| 0x721 | `SEB_STATUS` | low | SEB | SYS brake state machine sync |
| 0x7FC / 0x7FD / 0x7FE | heartbeats | high/low | Host / RT / SYS | Topology liveness in toolkit |

### 3.3 Signal names and limits (shared + contracts)

| Signal path | Limits / enums | Source |
|-------------|----------------|--------|
| Host / RT motor speed | fwd **3000** mm/s, rev **500** mm/s | `shared_config.h`, `host.yaml`, `rt.yaml` |
| Host yaw | **±3000** mrad/s | `host.yaml`; RT direct_resolver maps ×15 → ±45000 mdeg |
| Gear | **0=N, 1=D, 2=S, 3=R** | `host.yaml` / `rt.yaml` enum |
| Host cmd stale on RT | **500 ms** | `kHostCmdStaleTimeoutMs` |
| MTR cmd stale on 0x204 | **200 ms** in AUTO | `mtr-stm32` safety task |
| SES `target_angle_raw` | toolkit clamps **±450** (0.1° units → ±45°) | `control_intent` + RT angle pipeline |
| SES enables | toolkit forces **alignment_enable + control_enable = 1** | bypass vs RT state machine |
| SEB pressure raw | **0–100** | `kSebMaxPressureRaw`; SYS maps kPa≈ raw×50 |
| SEB stroke 0 mm raw | **600** = (0+30)/0.05 | `shared` stroke scale/offset; SYS/RT |

### 3.4 What each original ECU *listens for* (for command observation)

| Unit | Command it cares about (RX) | Your software-only check |
|------|----------------------------|---------------------------|
| **MTR** | **0x204** `motor_speed_mmps`, `gear` | Low motor channel; values match UI; ~10 ms |
| **SES (EPS_C)** | **0x169** vendor command | Low steering; enables ON; angle raw; ~20 ms |
| **SEB** | **0x7B9** vendor command | Low brake; alignment/control ON; pressure or stroke; ~20 ms |
| **RT** (as consumer of Host) | **0x300** | High method only |
| **SYS** (as consumer of RT brake) | **0x205** | *Not* emitted by toolkit Low direct; vehicle path is 0x205 → SYS → 0x7B9 |

Toolkit Low **brake** therefore matches the **wire to SEB** (what SYS/RT would finally TX), not the intermediate **0x205** RT→SYS hop.

---

## 4. Start software-only stack

```powershell
# Terminal 1 — API
cd C:\projects\etrike
npm run toolkit:api
# http://127.0.0.1:8001

# Terminal 2 — UI
cd C:\projects\etrike
npm run toolkit:ui
# http://127.0.0.1:5173
```

Confirm: top bar **Computer**, health **Healthy**, stream **Live**, Bench TX available.  
Details / stop / ports: [`../run.md`](../run.md).

---

## 5. Unit tests (backend — original protocol on the path)

```powershell
cd C:\projects\etrike\control-toolkit\backend
# once: python -m venv .venv; .\.venv\Scripts\activate; pip install -e ".[dev]"
.\.venv\Scripts\activate
python -m pytest -q
```

These tests load the monorepo `protocol/` package (`pyproject.toml` `pythonpath`) — same codecs firmware builds from.

Focused subsets:

```powershell
# Control shaping + Low direct + TX gate (aligns with control_intent / firmware limits)
python -m pytest -q tests/test_direct_actuator.py tests/test_keyboard_input.py tests/test_kinematics.py tests/test_tx_gate.py

# Virtual dual bus + decode pipeline
python -m pytest -q tests/test_virtual_transport.py tests/test_virtual_pipeline.py tests/test_router.py tests/test_decoder.py tests/test_encoder.py

# Protocol bridge / dictionary (YAML + vendor opaque SES/SEB)
python -m pytest -q tests/test_protocol_bridge.py tests/test_dictionary_catalog.py tests/test_bit_layout.py tests/test_firmware_alignment.py

# Sessions / stream / API surface
python -m pytest -q tests/test_sessions.py tests/test_bench_tx.py tests/test_api_surface.py tests/test_websocket_stream.py
```

Against a **running** API:

```powershell
python scripts/control_drive_probe.py
```

---

## 6. UI e2e (optional)

```powershell
cd C:\projects\etrike\control-toolkit\frontend
npm run test:e2e
# dedicated ports 8010 / 5174

npx playwright test e2e/live-click-audit.spec.ts --config=playwright.live.config.ts
# uses your live 5173 → 8001 stack
```

---

## 7. Manual recipes — observe commands (~10 s)

### Shared prep

1. Computer mode, stream Live.  
2. **Control → Enable Bench TX** (ON).  
3. Open **Live CAN** (Both buses). Use **Latest** for “is this ID live?” and **Stream** for continuity over 10 s.  
4. After each recipe: channel **Stop**, **Stop all**, or Drive **Disarm**.

Judgement rule: **match bus, ID, name, and engineering values** on the TX/echo path. **Ignore** missing 0x206 / 0x201 / 0x721 / heartbeats.

---

### Recipe A — Host high-level motion (emulates Host only)

**Firmware analogue:** Jetson / Host TX `HOST_DRIVE_CMD`; RT would consume it (`can_rx_router` case `kIdHostDriveCmd`).

**UI**

1. Control → **High**, or **Drive → Arm**.  
2. Hold forward (~W / throttle) **~10 s**, or analysis inject periodic  
   `speed_mmps=1500`, `yaw_rate_mrad_s=0`, `gear=1` (D), `period_ms=10`.  
3. Live CAN filter **high** / **0x300**.

**Pass**

| Check | Expected (from `host.yaml` + toolkit) |
|-------|----------------------------------------|
| Bus / ID / name | high · **0x300** · `HOST_DRIVE_CMD` |
| `speed_mmps` | > 0 while forward; clamp ≤ 3000 |
| `gear` | 1 (D) for forward intent |
| `yaw_rate_mrad_s` | ~0 if straight; non-zero if steering |
| Rate | ~10 ms while intent active |
| After release | speed 0 / job stopped |

**Will not appear (by design):** 0x204, 0x169, 0x7B9 — RT is not running.

**API (~10 s)**

```powershell
$base = "http://127.0.0.1:8001/api/v1"
# Enable Bench TX first using session_id + revision from GET $base/status
Invoke-RestMethod -Method POST -Uri "$base/analysis/host-drive" -ContentType "application/json" -Body (@{
  speed_mmps = 1500; yaw_rate_mrad_s = 0; gear = 1; period_ms = 10
} | ConvertTo-Json)
Start-Sleep -Seconds 10
Invoke-RestMethod -Method POST -Uri "$base/analysis/stop" -ContentType "application/json" -Body "{}"
```

---

### Recipe B — MTR unit command (what MTR firmware RX on 0x204)

**Firmware analogue:** `mtr-stm32` `process_can_frame` → `RtDriveCmd` → `g_cmd_speed_mmps` / `g_cmd_gear`.  
On vehicle this frame is **TX by RT** after resolving Host; toolkit Low **motor** encodes the **same contract** (`rt:rt_drive_cmd`).

**UI**

1. Control → **Low** → Motor.  
2. e.g. speed **800**, gear **D** → **Start** → **10 s** → **Stop**.  
3. Live CAN **low** / **0x204**.

**Pass**

| Check | Expected |
|-------|----------|
| Bus / ID / name | low · **0x204** · `RT_DRIVE_CMD` |
| DLC | **5** (contract) |
| `motor_speed_mmps` | matches card (clamped −500…3000) |
| `gear` | 0…3 as selected |
| Period | ~**10 ms** |
| Control line | `direct-motor-tx` shows live |

No **0x206** feedback required. On real MTR, missing 0x204 for >200 ms in AUTO would stale — here we only prove the command stream is continuous.

---

### Recipe C — Steer-by-wire unit command (SES / EPS_C on 0x169)

**Firmware analogue:** `rt-esp32` `SteeringControl` + `can::custom::ses::encode_command` → **0x169** @ 50 Hz when not MANUAL and state allows.  
Toolkit Low **steering** uses the same catalog key `ses:vcu_ses_req` and forces enables ON (bypasses RT BOOT/LISTEN/FAULT silence).

**UI**

1. Control → **Low** → Steering → set angle → **Start** → 10 s → **Stop**.  
2. Live CAN **low** / **0x169**.

**Pass**

| Check | Expected |
|-------|----------|
| Bus / ID / name | low · **0x169** · `VCU_SES_REQ` |
| Period | ~**20 ms** |
| `alignment_enable` / `control_enable` | **1** (toolkit forced) |
| `target_angle_raw` | matches UI (clamped ±450) |

No **0x201** SES_STATUS required.

---

### Recipe D — Brake-by-wire unit command (SEB on 0x7B9)

**Firmware analogue:**

- **SYS** `BrakeControl::build_command` → 0x7B9 from lever / ESTOP / pressure from **0x205** (`sys-esp32/brake_control.h`).  
- **RT** may also TX 0x7B9 in AUTO when steer ACTIVE (`make_seb_auto_req`) or on SYS loss (`make_seb_takeover_req`).

Toolkit Low **brake** encodes `seb:vcu_seb_req` with alignment/control ON — same **ID and vendor layout** SEB receives; skips SYS boot/LISTEN_SYNC and the **0x205** intermediate.

**UI**

1. Control → **Low** → Brake → pressure (e.g. 40 raw) → **Start** → 10 s → **Stop**.  
2. Live CAN **low** / **0x7B9**.

**Pass**

| Check | Expected |
|-------|----------|
| Bus / ID / name | low · **0x7B9** · `VCU_SEB_REQ` |
| Period | ~**20 ms** |
| enables | alignment + control ON |
| pressure / mode | matches card (pressure mode default in toolkit) |

No **0x721** SEB_STATUS required.

**Note:** To exercise the **true SYS path** (0x205 → 0x7B9) you need **SYS firmware** (or a future synthetic SYS). Toolkit does not TX 0x205 on Low direct today.

---

### Recipe E — Combined unit pose ~10 s (MTR + SES + SEB commands)

Still observation-only; still **Low method** (cannot combine with active High Host job).

1. Bench TX ON.  
2. Control → **Low**.  
3. Start **motor + steering + brake** with known values.  
4. Hold **~10 s**.  
5. Live CAN (low): confirm concurrent:

| ID | Unit under test |
|----|-----------------|
| **0x204** | MTR command |
| **0x169** | SES command |
| **0x7B9** | SEB command |

6. Stop all.

This is the software-only stand-in for “what the three actuation units should see,” **without** claiming RT ran kinematics from 0x300.

---

### Recipe F — ESTOP

1. Start any High or Low motion.  
2. Header **ESTOP**.  
3. Expect motion release + dual-bus **0x001** `SAFETY_ESTOP` inject (MTR/SYS/… contract receivers include MTR).  
4. Re-enable Bench TX before further TX tests if needed.

---

## 8. How to read the bus (no feedback)

### Live CAN (UI)

Filter by hex ID or name; open row detail for signals / hex.

### History

```powershell
(Invoke-RestMethod "http://127.0.0.1:8001/api/v1/history?limit=100").frames |
  Where-Object { $_.can_id -in 0x300,0x204,0x169,0x7B9,0x205 } |
  Select-Object bus, can_id, data_hex, direction, source -First 30
```

### Latest decoded state

```powershell
(Invoke-RestMethod "http://127.0.0.1:8001/api/v1/state").messages |
  Where-Object { $_.can_id -in 768,516,361,1977,517 } |  # 0x300,204,169,7B9,205
  ForEach-Object {
    [pscustomobject]@{
      name = $_.name; bus = $_.bus
      id = ('0x{0:X}' -f $_.can_id); fresh = $_.freshness
    }
  }
```

### Recording

Diagnostics → record **while** TX is active → stop → evidence. Quiet bus after Stop-all ⇒ 0 frames is normal.

Stopped recordings also expose **Export CANalyzer**. The downloaded ZIP includes
BLF + High/Low DBC + metadata and can be inspected in Vector CANalyzer without
changing the toolkit runtime transport.

---

## 9. “Full system software-only” without all ECUs

| Capability | Computer mode |
|------------|---------------|
| Dual virtual High + Low | Yes |
| Encode/decode via **original** `protocol/` codecs | Yes |
| Host role (0x300) | Yes |
| Unit command roles (0x204 / 0x169 / 0x7B9) via Low direct | Yes |
| Bench TX, ownership, stop-all, dictionary | Yes |
| Observe TX as RX | Yes |
| RT kinematics / mode gates / steer lockout on 0x204 | **No** (firmware only) |
| SYS brake state machine + 0x205→0x7B9 | **No** |
| MTR DAC/gear hardware loop + 0x206 | **No** |
| SES/SEB vendor status loops | **No** |
| Network same_frame gateway | **No** (not emulated in toolkit) |

**Practical full-stack software-only procedure:**

1. `pytest` green (protocol + control locks).  
2. Recipe **A** — Host command correct.  
3. Recipes **B–D** (or **E**) — unit commands correct for MTR / SES / SEB wire contracts.  
4. Optional ESTOP.  
5. Sign off **command path**. Defer closed-loop and RT/SYS behavior to firmware / HIL / `native-test` / `simulation`.

### Original code trees (for deeper ECU tests)

| Tree | Role | How it relates to this guide |
|------|------|------------------------------|
| `protocol/` | Wire SoT | Toolkit encode path |
| `control-toolkit/` | Bench Host + unit inject | This guide |
| `rt-esp32/` | Host→actuators + SES TX + optional SEB | Not run in toolkit; use PlatformIO / native |
| `sys-esp32/` | Mode, lights, **0x7B9** from lever/0x205 | Not run in toolkit |
| `mtr-stm32/` | Motor / gear / DAC; RX **0x204** | Not run in toolkit |
| `native-test/`, `simulation/` | Hosted logic tests | Repo `docs/testing-guide.md` |

---

## 10. Hardware track (contrast)

| Profile | Transport |
|---------|-----------|
| `pure_software` | Virtual High/Low |
| `bench_test` / `full_vehicle` | CANalyst CH0=High, CH1=Low — **same** APIs and frame IDs |

Real mode **refuses** silent fallback to virtual if adapter missing. On hardware, Recipe A can be paired with a real RT so Low frames appear “for free”; that is HIL, not Computer mode.

---

## 11. Suggested regression order

1. Backend `pytest -q`.  
2. Stack Healthy + Live.  
3. **A** — 0x300 Host.  
4. **B** — 0x204 MTR cmd.  
5. **C** — 0x169 SES cmd.  
6. **D** — 0x7B9 SEB cmd.  
7. **E** — all three Low together.  
8. Drive Arm + W (same High path as A).  
9. ESTOP.  
10. Optional Playwright.

---

## 12. Pass / fail cheat sheet

| Observation | Meaning |
|-------------|---------|
| No TX | Bench TX off, no session, or API offline |
| 409 on control | Bench TX / ownership / sequence / High↔Low exclusivity |
| Only 0x300 | High method — **correct**; RT not in process |
| Only 0x204/169/7B9 | Low method — **correct** for unit observation |
| Expect 0x205 from Low motor/steer/brake | Wrong expectation — toolkit does not TX 0x205 on direct |
| Topology all offline | Expected without heartbeats |
| Confirmed HMI mode/power `—` | No peer ACK — OK for command-only |
| Stream Lost | Fix stack (`run.md`), not protocol |

---

## 13. Quick ID table

| Hex | Dec | Bus | Observe for |
|-----|-----|-----|-------------|
| 0x300 | 768 | High | Host drive command |
| 0x204 | 516 | Low | **MTR** command (`RT_DRIVE_CMD`) |
| 0x169 | 361 | Low | **SES** command (`VCU_SES_REQ`) |
| 0x205 | 517 | Low | RT→SYS brake (vehicle / RT only; not toolkit Low direct) |
| 0x7B9 | 1977 | Low | **SEB** command (`VCU_SEB_REQ`) |
| 0x206 | 518 | Low | MTR feedback (optional later) |
| 0x721 | 1825 | Low | SEB feedback (optional later) |
| 0x001 | 1 | High+Low | SAFETY_ESTOP |

---

## 14. Mental model (one sentence)

**Computer mode reuses the original protocol codecs and can play Host (0x300) or play the Low command wires (0x204 / 0x169 / 0x7B9) that MTR, SES, and SEB firmware are written to consume; it does not run RT/SYS, so High-level Host motion does not automatically expand into Low unit frames until real RT (or a future twin) is in the loop.**

*Command-path software-only testing passes when those frames match the contracts and the intended recipe. ECU reaction to those frames is a separate firmware / HIL concern.*
