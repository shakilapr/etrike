# VCU_2 / MTR — STM32G431 Motor & Relay Actuator Node: Architecture

> **ECU Role:** Motor Control Unit (MTR / Actuator Node)
> **Silicon:** STMicroelectronics STM32G431CBU6 (Arm Cortex-M4F @ 16 MHz, 128 KB Flash, 32 KB SRAM)
> **Bus:** CAN Low Bus (Classic CAN 2.0A @ 500 kbit/s via internal FDCAN1 + external transceiver)
> **Language & Standard:** C++17 (`arm-none-eabi-g++ -std=gnu++17`, `-O2`), header-only actuator stack
> **Toolchains:** Dual-workflow — **STM32CubeIDE** (GUI project / ST-LINK debug) & **PlatformIO** (`pio run -e vehicle -t upload`, DFU)
> **Canonical protocol:** Generated codecs from `protocol/contracts/*.yaml` (`protocol/generated/cpp/etrike_protocol.hpp` via `protocol/compat/can.hpp`); generation verified at build time by `protocol/tools/pio_prebuild_canonical.py`

> **Revision status (matches code as of Oct 2026):**
> - CAN is configured **receive-only by default** (`kEnableCanBroadcast = false`): periodic telemetry TX is compiled out.
> - RX filter set is exactly **0x001, 0x011, 0x110, 0x113, 0x204**. Legacy `0x0BB`/`0x0AA` fallback frames are **no longer accepted**.
> - Persistent E-stop authority is **0x011 SYS_SAFETY_STS** (E2E-CRC protected, redesigned estop_source/reason layout). E-stop is **never** cleared by 0x204/0x113 sequences — only by the asymmetric two-frame 0x011 clear plus a REARM power cycle.
> - Dedicated **0x204 drive-command watchdog (150 ms)** runs alongside the generic **500 ms comms deadman**.
> - **IWDG independent watchdog (~1.6 s)** guards the main loop; reset cause is surfaced as a diagnostic.
> - Diagnostics plane: shared `DiagnosticManager` drains **0x631 MTR_DIAG_EVENT_RPT** reports.

---

## 1. System Overview

`mtr-stm32` is the traction actuator and relay controller. It accepts propulsion authority only from supervised CAN command streams on the Low bus and translates them into:

1. **Relay outputs** — 3 active-low drivers: Ignition (PA4), Drive (PA2), Reverse (PA0), with strict Drive↔Reverse mutual exclusion.
2. **Analog throttle** — MCP4725 12-bit DAC over bit-banged I2C (PA5 SCL / PA7 SDA), clamped to 0.8 V…2.4 V in motion, forced 0.0 V otherwise.

### 1.1 Command Sources (source-agnostic actuator)

MTR is the lowest-level actuator; it does not distinguish command masters. Both topologies deliver the same canonical frames:

```
Topology 1 (Autonomous)              Topology 2 (RM test-bench bypass)
Jetson ─► RT ESP32-S3                FlySky RC ─► RM ESP32
            │ 0x204 RT_DRIVE_CMD                    │ 0x204 RT_DRIVE_CMD
            ▼                                        │ 0x110 SYS_MODE_CMD (emulated)
        MTR STM32                                    └─ 0x113 SYS_PWR_CMD (emulated)
```

- **Mode authority** (`0x110`), **power authority** (`0x113`), **persistent safety state** (`0x011`) and the **hardwired ESTOP broadcast** (`0x001`) are honored from any sender.
- All rolling-counter streams are supervised by `StreamValidity` (`shared/stream_validity.h`): duplicate/frozen counters do not refresh freshness; gaps > 2 or reorder faults invalidate authority; reacquisition requires a baseline frame plus one advancing frame.

### 1.2 Receive-Only Broadcast Mode

`src/config.h:16` sets `kEnableCanBroadcast = false`: the fitted transceiver is receive-only, and any TX attempt produces bit errors on PA12 → Bus-Off. Therefore:

- The periodic TX blocks in `main.cpp` (0x120 / 0x206 / 0x502) are compiled out via `if constexpr`.
- The **0x631 diagnostic drain is gated on the same flag** — receive-only mode emits nothing: a diagnostic TX attempt into a non-ACKing bus racks TEC on every try, and the accumulated churn reproduces the bus-off/recovery cycle that halts RX and trips the drive-command watchdog. Pending reports accumulate in the bounded `DiagnosticManager` queue until TX is enabled.
- TX-capable builders remain in the code (`build_throttle_status_frame`, `build_motor_feedback_frame`, `fill_node_status`) and are exercised by native tests.

### 1.3 Bench TX Diagnostic Probe (`kTxProbe`)

Until the physical TX path is proven, a bench-only probe (`constexpr bool kTxProbe`, `config.h:77`, default `false`) sends **one 0x206 per second** and reports the outcome on the PC6 status LED — the only output channel available when TX itself is broken, since a failing frame cannot carry its own error report onto the bus. Verdict coding (judge ≥3 s after power-on; LED starts solid):

| PC6 LED | Meaning |
|---|---|
| **Solid ON** | Frame transmitted: TXBRP drained, TEC unchanged, LEC clean → TX path proven |
| **Slow blink** (1 Hz) | LEC 3 — ACK error: frame reached the bus, nobody acknowledged (partner/termination) |
| **Fast blink** (5 Hz) | LEC 1/2/4/5/6 — bus-level error: transceiver TX stage dead, silent, or miswired |
| **Dark** | Bus-Off latched (controller disconnected itself) |

Mechanics: verdict comes from the `CanDriver` TX-health shadow (`service_recovery()` is the **single PSR/ECR reader** — PSR.LEC is read-destructive — exposing `tx_health()`/`tx_pending()`); rate-coded patterns avoid human flash-counting (no double/triple-blink discrimination). Probe builds compile out the PC6 relay-transition toggle and the 0x631 drain; all production safety logic (authority supervision, watchdogs, failsafe) runs unchanged. The 1 Hz rate violates the 0x206 20 ms contract — keep RT offline (bench topology 2) or expect RT-side staleness flags.

---

## 2. Hardware Pin Map

| Pin | Function | Mode | Logic / Polarity | Description |
|---|---|---|---|---|
| **PA0** | Mode Reverse Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (72 V Rev), SET = Relay OFF |
| **PA2** | Mode Drive Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (72 V Drive), SET = Relay OFF |
| **PA4** | Ignition Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (Ignition ON), SET = Relay OFF |
| **PA5** | SW-I2C SCL | Output Open-Drain (pull-up) | Idle HIGH | MCP4725 DAC clock |
| **PA7** | SW-I2C SDA | Output Open-Drain (pull-up) | Idle HIGH | MCP4725 DAC data (ACK sampled low) |
| **PA11** | FDCAN1_RX | AF9 | — | CAN RX from transceiver |
| **PA12** | FDCAN1_TX | AF9 | — | CAN TX to transceiver (receive-only HW) |
| **PC6** | Status LED | Output Push-Pull | **Active-Low** | Toggles on relay-state transitions |

**Power-up safe state** (`relay_controller.h:24-43`, `dac_controller.h:17-47`): relay pins written `SET` (OFF) *before* GPIO configuration (no boot glitch), LED off, I2C lines high; DAC EEPROM is provisioned to 0.0 V at first boot so the DAC natively powers up at 0 V.

---

## 3. Clock, Timing & CAN Bit Timing

- **Core:** 16 MHz HSI direct, no PLL (`RCC_PLL_NONE`, `main.cpp:116-139`). `SYSCLK = HCLK = PCLK1 = PCLK2 = 16 MHz`, flash latency 0, `PWR_REGULATOR_VOLTAGE_SCALE1`.
- **FDCAN kernel clock:** PCLK1 = 16 MHz (`can_driver.h:26-32`).
- **Bit timing (500 kbit/s, 81.25 % sample point, `can_driver.h:54-60`):**
  - Nominal prescaler 2 → t_q = 125 ns; Seg1 = 12 t_q, Seg2 = 3 t_q, SJW = 3.
  - 1 + 12 + 3 = 16 t_q = 2.0 µs → 500 kbit/s. Sample point 81.25 %, aligned with the ESP32 TWAI nodes (80.0 %, SJW = 3).
- **Loop cadence:** `HAL_Delay(1)` polling loop (`main.cpp:292`) with a 5 ms control tick (`kMainLoopPeriodMs`, `config.h:48`).
- **DWT cycle counter** enabled at boot (`main.cpp:156-158`).

---

## 4. Software Architecture

```
mtr-stm32/
├── platformio.ini          # env:vehicle (firmware), env:native (Unity host tests)
├── STM32G431CBUX_FLASH.ld  # 128 KB flash / 32 KB RAM linker script
├── VCU_2.ioc               # CubeMX project (VCU_2)
├── Core/                   # ST vendor scaffolding (startup, MSP, IRQ dispatch, syscalls)
├── src/                    # Actuator stack (all subsystems header-only except main.cpp)
│   ├── config.h            # Pins, DAC limits, timings, watchdog constants
│   ├── can_driver.h        # FDCAN1 driver: exact-match filters, FIFO0→32-slot ring, TX FIFO, bus-off recovery
│   ├── relay_controller.h  # Active-low relay FSM (Off/Park/Drive/Reverse) with mutual exclusion
│   ├── dac_controller.h    # Bit-banged I2C MCP4725: EEPROM provisioning, clamp window, zero refresh
│   ├── motor_manager.h     # Authority supervision, E-stop latch/clear, REARM, throttle mapping, telemetry builders
│   └── main.cpp            # Boot (reset-cause, clock, init, IWDG) + 1 ms poll loop / 5 ms tick
└── test/
    ├── stub/stm32g4xx_hal.h           # HAL stub for host builds
    ├── test_full_suite/test_mtr_full_suite.cpp  # 18-test comprehensive native suite (Unity, env:native)
    └── legacy/…                       # Older standalone suites (flat test_*.cpp; excluded from env:native)
```

### 4.1 Boot Sequence (`main.cpp:142-207`)

1. **Capture reset cause before it is cleared**: `RCC->CSR.IWDGRSTF`; if the independent watchdog reset the MCU, raise-then-recover `DiagId::MtrWatchdogReset` (one-shot boot notification, `main.cpp:147-150,178-182`).
2. `HAL_Init()`, DWT cycle counter enable, `SystemClock_Config()` (HSI 16 MHz).
3. `g_motor.init()` → relays to safe OFF + DAC init (EEPROM 0 V provisioning).
4. `g_can.init()` → on failure `Error_Handler()` (`__disable_irq()` + halt; outputs remain in the safe OFF state established above).
5. Wire shared `DiagnosticManager` into MotorManager and CanDriver (reporting only — bookkeeping never changes safety reactions).
6. Start **IWDG**: LSI ~32 kHz / 128 = 250 Hz (4 ms/tick), reload 400 → **~1.6 s window** (`main.cpp:190-205`). Cannot be stopped except by reset. If it cannot start → `Error_Handler()`.
7. Enter main loop.

### 4.2 Main Loop Schedule (`main.cpp:209-293`)

| Rate | Work |
|---|---|
| Every pass (~1 ms) | Drain `poll_rx()` ring → `g_motor.handle_frame()` per frame |
| Every 5 ms | `g_can.service_recovery()` (bus-off + TX-health shadow refresh), `g_motor.tick()` (safety evaluation), TX-probe tick (`kTxProbe` builds only), bounded diagnostic drain (≤ 8 × `0x631` frames/iteration, gated on `kEnableCanBroadcast`) |
| Every 10 ms *(compiled out: `kEnableCanBroadcast=false`)* | 0x120 SYS_THROTTLE_STS |
| Every 20 ms *(compiled out)* | 0x206 MTR_MOTOR_FBK; 0x502 MTR_NODE_STATUS (10 ms phase offset) |
| End of every full cycle | `HAL_IWDG_Refresh()` — fed **only** after CAN drain + `tick()` + periodic sends complete; a hang anywhere above resets the MCU into the safe all-OFF boot state |

### 4.3 CAN Driver (`src/can_driver.h`)

- **Instance:** FDCAN1, Classic CAN, normal mode, `AutoRetransmission = DISABLE`, TX pause & protocol exception disabled, TX FIFO operation mode (`can_driver.h:47-69`).
- **Acceptance:** 5 exact-match standard filters routed to RX FIFO 0 (`FilterID2 = 0x7FF` mask): `0x001`, `0x011`, `0x110`, `0x113`, `0x204`. Global filter **rejects** non-matching standard frames and remote frames (`can_driver.h:76-84`).
- **RX path:** FIFO0 new-message IRQ (NVIC priority 5 — below HAL critical sections at priority 0) → ISR drains FIFO0 into a **32-slot ring buffer** (`rx_ring_`); overflow increments `rx_overflow_` and raises `DiagId::MtrFdcanRxOverflow`. Main loop `poll_rx()` pops under an IRQ-disable critical section (`can_driver.h:135-186`, ring at `:286`).
- **Bus-off / HAL-error recovery** (`service_recovery`, `can_driver.h:175-224`, called at 5 ms): debounced to one attempt per **150 ms**; on `PSR.BO` clears `CCCR.INIT` (triggering the ISO 11898-1 128×11 recessive-bit recovery), repairs a corrupted `HAL_FDCAN_STATE_ERROR` by forcing `READY` + restart, re-arms the FIFO0 notification, and reports `DiagId::MtrFdcanBusOff` with a BITFIELD16 snapshot (TEC<<8 | REC). Also the **single PSR/ECR reader**: every call refreshes the `CanTxHealth` shadow (TEC, REC, LEC, bus-off) exposed via `tx_health()` / `tx_pending()` — PSR.LEC is read-destructive, so no other code may read PSR.

---

## 5. Canonical CAN Protocol

### 5.1 RX (filter-accepted set)

| ID | Frame | DLC | Cycle | Producer | Role |
|---|---|---|---|---|---|
| 0x001 | `SAFETY_ESTOP` | 0 | event | any | Hardwired ESTOP broadcast → immediate latch |
| 0x011 | `SYS_SAFETY_STS` | 5 | 200 ms | SYS | Persistent (latched) E-stop authority, E2E-protected |
| 0x110 | `SYS_MODE_CMD` | 2 | 100 ms | SYS | Mode authority (`mode` 0=MANUAL/1=AUTO + rolling_counter) |
| 0x113 | `SYS_PWR_CMD` | 2 | 100 ms | SYS | Power authority (`power_state` 0=OFF/1=ON + rolling_counter) |
| 0x204 | `RT_DRIVE_CMD` | 5 | 10 ms | RT (or RM on bench) | `motor_speed_mmps` int32 [-500, 3000] + `gear` (0=N, 1=D, 2=S, 3=R) |

**0x011 SYS_SAFETY_STS layout** (generated codec `SysSafetySts`, DLC 5): `estop_source` (b0: 0=none, 1=SYS-local, 2=0x001-origin, 3=peer fault), `node_presence` (b1), `estop_reason` (b2: 0=none, 1=HW button, 2=RT heartbeat lost, 3=0x001, 4=MTR fault, 5=EGAS fault, 6=task deadline, 7=CAN bus-off), `rolling_counter` (b3), `e2e_crc` (b4).
**E2E check:** CRC-8/H2F over payload bytes [0..3] with data ID `0x3C11` (`protocol/compat/e2e.hpp:21,49-51`). Mismatch → invalidate stream, raise `MtrSysSafetyCrcError`.

### 5.2 TX (built but suppressed in receive-only mode; exercised by tests)

| ID | Frame | DLC | Rate | Payload |
|---|---|---|---|---|
| 0x120 | `SYS_THROTTLE_STS` | 2 | 100 Hz / 10 ms | `speed_mmps` int16 (echoed setpoint, 0 when inhibited) |
| 0x206 | `MTR_MOTOR_FBK` | 4 | 50 Hz / 20 ms | `motor_command_speed_mmps` int16, `gear_state` u8 (N/D/R; S reports as D), `fault_flags` u8 |
| 0x502 | `MTR_NODE_STATUS` | 8 | 50 Hz / 20 ms | `node_state` (4-bit enum), `block_mask` u16 (b1-2), status bits (b3), reserved (b4-5), `rolling_counter` (b6), `e2e_crc` (b7 = CRC-8/H2F over bytes 0..6, data ID 0) |
| 0x631 | `MTR_DIAG_EVENT_RPT` | 8 | on event | `diag_id` u16, `state` u8, `occurrence_count` u8, `report_counter` u8, `flags` u8, `snapshot_data` u16 |

**0x206 fault_flags:** bit0 `kMtrFaultEstopActive` (latched ESTOP — redundant confirmation to SYS), bit1 `kMtrFaultCmdTimeout` (comms OR drive-cmd timeout), bit4 `kMtrFaultStartupReady` (always set).

**0x502 semantics (strictly observational** — never clears ESTOP, re-arms ignition or grants authority, `motor_manager.h:485-521`):
- `node_state`: ESTOP(latched) > RECOVER(rearm pending) > INHIBITED(!ignition or inhibited) > ACTIVE(AUTO + gear engaged) > STANDBY.
- `block_mask`: 0x0001 estop latch, 0x0002 comms timeout, 0x0004 drive-cmd timeout, 0x0008 REARM pending, 0x0010 authority invalid (0x110/0x113/0x011), 0x0020 ignition OFF.
- Status bits (byte 3): estop_active, ready, command_received, command_nonzero, output_enabled, estop_latched, recovery_pending, degraded.

---

## 6. Authority & Supervision Model (`src/motor_manager.h`)

### 6.1 Multi-Stream Readiness Latch

```
MTR_READY_MODE  (bit0) ← fresh 0x110 with valid rolling counter
MTR_READY_POWER (bit1) ← fresh 0x113, valid counter, power semantics (+REARM gate)
MTR_READY_DRIVE (bit2) ← valid 0x204 within the dedicated watchdog window
```

`is_motor_ready()` requires **all three** bits (`motor_manager.h:18-23,76-78`); any authority loss clears the mask (`on_authority_loss()`).

- **Mode stream:** `mode_val_` freshness timeout `kAuthFreshMs = 5 × SysModeCmd::kCycleMs = 500 ms`. Invalid → keep last mode, inhibit 0x204 (`motor_manager.h:100-117`). A 0x204 arriving while mode is invalid is dropped and raises `DiagId::MtrCmdStreamUnauthorised` (`motor_manager.h:122-126`). `0x110` **no longer carries ESTOP** — the E-stop state lives solely in `0x011`.
- **Power stream:** `pwr_val_` same 500 ms freshness. Invalid → power-safe. OFF edge feeds the REARM sequence (§6.3).
- **Safety stream (0x011):** `safety_val_` freshness `kSafetyFreshMs = 700 ms` — deliberately *not* a multiple of the 200 ms cycle so a clock-doubling fault cannot appear fresh (`motor_manager.h:587-591`). Expiry inside `tick()` (checked every 5 ms, not only on frame arrival) forces invalidation + `DiagId::MtrSysSafetyStsTimeout` (`motor_manager.h:301-312`).
- **Generic comms deadman:** any CAN frame refreshes `last_rx_ms_`; **500 ms** of total silence sets `comms_timed_out_` and drops all authority (`motor_manager.h:288-293`).

### 6.2 Ignition Derivation

```
ignition_on_ = power_valid_ && power_state_on_ && safety_state_valid_
            && (!rearm_required_ || rearm_observed_)          // motor_manager.h:313-314
```

### 6.3 E-Stop Latch, Asymmetric Clear & REARM

**Latch** (`trigger_estop`, `motor_manager.h:438-451`) on: `0x001` frame, or `0x011` with `estop_source != 0`. Effects: relays → `State::Off`, DAC → 0 V, targets zeroed, shift dwell cleared, authority mask cleared, and the clear-sequence state reset (a stale pre-latch zero can never pair with a post-latch zero).

**Asymmetric clear** (`handle_safety_status`, `motor_manager.h:231-263`): an authorized clear requires **two consecutive zero frames whose rolling counters advance by exactly +1**. Duplicate/gap/jump restarts the sequence with that frame as the new baseline. Continuous zeros while *not* latched never accumulate credit. Clear releases only on `clear_confirm_ >= 2`.

**Authorized clear** (`authorized_clear`, `motor_manager.h:268-284`): unlatches ESTOP but invalidates mode/power streams (`invalidate_now()`) and sets `rearm_required_ = true` — a stale cached 0x204/0x110/0x113 cannot re-enable motion.

**REARM gate:** propulsion stays inhibited until a fresh **0x113 OFF→ON edge** is observed (with a fresh 0x110 seen meanwhile) → `rearm_observed_ = true` (`motor_manager.h:165-181`). The `rearm_off_seen_` edge observed during the ESTOP carries across the clear. If REARM is not completed within **`kRearmTimeoutMs = 10 s`** of the clear, `DiagId::MtrRearmSequenceViolation` is raised (report-only; the vehicle stays inhibited) (`motor_manager.h:360-371,611`). An ON without a prior observed OFF edge also raises the violation (`motor_manager.h:173-177`).

### 6.4 Dedicated 0x204 Drive-Command Watchdog (issue #2)

Independent of the generic deadman so authority traffic cannot keep a frozen throttle alive (`config.h:50-60`, `motor_manager.h:316-350`):

- **Armed only while drive is *expected*:** AUTO mode + power ON + valid mode/safety/power authority + REARM satisfied.
- **Trip:** no valid 0x204 for **150 ms** while expected (reference = last 0x204 receive time; if none ever arrived, the arm time) → latch `drive_cmd_timed_out_`, clear `MTR_READY_DRIVE`, raise `DiagId::MtrRtDriveCmdTimeout`.
- **Confirmed recovery:** **3** consecutive valid 0x204 receive events, each separated by ≤ **100 ms** (a larger gap restarts the count at 1).
- Disarmed (drive not expected): trip cleared, recovery state reset.

### 6.5 Propulsion-Inhibit Predicate (single source of truth)

`propulsion_inhibited()` (`motor_manager.h:533-539`) — true if any of: ESTOP latched, comms timeout, drive-cmd timeout, power/safety authority invalid, REARM pending, ignition off, gear N, or readiness mask incomplete. Zeroes the echoed setpoint in 0x120/0x206 and selects the `node_state` for 0x502.

---

## 7. Actuation Pipeline

### 7.1 Per-Tick Fail-Safe (first branch of `tick()`, `motor_manager.h:352-372`)

If ESTOP ∨ comms-timeout ∨ drive-timeout ∨ !power_valid ∨ !safety_valid ∨ REARM-pending ∨ !ready → targets zeroed, `relays_.set_state(Off)`, `dac_.force_zero()`, return.

### 7.2 Gear Shift Arc Protection (D↔R)

A direct D↔R transition forces a **50 ms neutral dwell** (`kShiftDwellMs`, `motor_manager.h:614,380-399`): DAC forced 0 V, relays dropped to `Park` (`Gear::N`), and only after the dwell expires is the new gear applied.

### 7.3 Relay Controller (`src/relay_controller.h`)

| State | Ignition (PA4) | Drive (PA2) | Reverse (PA0) |
|---|---|---|---|
| Off (safe default) | OFF (SET) | OFF | OFF |
| Park (Neutral) | ON (RESET) | OFF | OFF |
| Drive | ON | ON | OFF (**mutual exclusion**) |
| Reverse | ON | OFF (**mutual exclusion**) | ON |

- Canonical gear mapping (`set_gear`, `relay_controller.h:57-75`): `D`/`S` → Drive, `R` → Reverse, `N`/default → Park; `ignition_on == false` forces Off. PC6 LED toggles on every state change (except TX-probe builds, where the LED is reserved for the probe verdict — §1.3).
- Drive and Reverse relays can **never** be energized simultaneously (structural in `apply_state_`).

### 7.4 Throttle Mapping → MCP4725 DAC

**Speed magnitude selection** (`motor_manager.h:413-428`): in Drive only positive setpoints count; in Reverse both canonical negative and legacy positive magnitudes are accepted. Setpoints below `shared::kLowSpeedThreshMmps = 50` mm/s map to 0 (avoids deadband jitter at the 0.8 V idle floor) (`motor_manager.h:548`).

**DAC code computation** (`motor_manager.h:542-559`):

```
norm    = clamp(speed_mag / 3000, 0, 1)
code    = 700 + norm × (1966 − 700)      # kDacActiveFloor = 700 ≈ 0.855 V
clamp(code, kDacMinCode=655, kDacMaxCode=1966)   # 0.8 V … 2.4 V @ 5.0 V ref
```

**DAC controller** (`src/dac_controller.h`):
- Bit-banged I2C on PA5/PA7 (open-drain, pull-ups); power-on **bus recovery** (9 SCL pulses with SDA high) + 50 ms rail settle (`:29-33,145-154`).
- **EEPROM provisioning:** burns 0.0 V into MCP4725 non-volatile memory at boot / first `force_zero()` contact (command `0x60` = DAC register + EEPROM, 50 ms `t_prog` wait) so future hardware power-ups default to 0 V even before MCU code runs; falls back to DAC-register-only writes (`0x40`) (`:40-46,101-108,202-214`).
- Candidate 7-bit addresses `0x60`, `0x61`, `0x62` with NACK-invalidated address caching (`:111-136`).
- `set_throttle(code, enabled)` clamps into [655, 1966]; anything else → `force_zero()`.
- `force_zero()` guarantees 0.0 V with a **periodic refresh every ~250 ms** (50 × 5 ms ticks) to survive brown-out/hot-plug of the DAC (`:70-94,216`).

---

## 8. Diagnostics Plane (Phase B)

Shared `DiagnosticManager` (`shared/diagnostics.h`) wired into MotorManager + CanDriver (`main.cpp:172-173`). Reporting only — raising/recovering never changes a safety reaction. Reports drain to the bus as **0x631 MTR_DIAG_EVENT_RPT**, capped at 8 frames per 5 ms iteration (`main.cpp:231-249`), **gated on `kEnableCanBroadcast`** — in receive-only mode pending reports accumulate in the bounded queue instead of being emitted into a bus that cannot ACK them.

| DiagId | Code | Raised when |
|---|---|---|
| `MtrRtDriveCmdTimeout` | 0x0301 | 0x204 stale 150 ms while drive expected |
| `MtrSysSafetyStsTimeout` | 0x0305 | 0x011 stream silent > 700 ms |
| `MtrSysSafetyCrcError` | 0x0306 | 0x011 E2E CRC mismatch |
| `MtrSysSafetyCounterStale` | 0x0307 | 0x011 rolling-counter fault/duplicate expiry |
| `MtrRearmSequenceViolation` | 0x030A | REARM OFF→ON edge missing (incl. 10 s timeout) |
| `MtrFdcanBusOff` | 0x030D | Bus-off / HAL error with TEC/REC snapshot |
| `MtrFdcanRxOverflow` | 0x030E | FIFO0 ring overflow counter |
| `MtrCmdStreamUnauthorised` | 0x0311 | 0x204 received without valid mode authority |
| `MtrWatchdogReset` | 0x0314 | IWDG reset cause at boot (raised+recovered once) |

(`DiagId` also defines `MtrSysModeCmdTimeout` 0x0308, `MtrSysPwrCmdTimeout` 0x0309, `MtrSpeedSetpointInvalid` 0x030F — present in the shared enum but not raised by current MTR firmware; mode/power staleness is handled silently by `StreamValidity` invalidation.)

---

## 9. Verification & Build

### 9.1 Firmware (`pio run -e vehicle -t upload`)

- Board `genericSTM32G431CB`, framework `stm32cube`, toolchain `gccarmnoneeabi ~1.100301`, upload via **DFU** (`0x08000000:leave`).
- `build_src_filter`: `src/*.cpp` (main.cpp) + Core MSP/IRQ/system/syscalls + startup `.s`.
- Prebuild script verifies the generated canonical codecs match the YAML contracts (`protocol.py generate --check`) — builds fail on protocol drift.

### 9.2 Native host tests (`pio test -e native`)

Unity environment restricted (via `test_filter = *full_suite*`) to `test/test_full_suite/test_mtr_full_suite.cpp` against the HAL stub (`test/stub/stm32g4xx_hal.h`) — the production headers are tested unmodified. 18 tests:

relay mutual exclusion · DAC software-I2C & clamps · DAC default-state-zero & late power-on · ESTOP & recovery · direction-shift dwell · comms watchdog · DAC curves · DAC golden vectors · mode-cmd does not clear ESTOP · asymmetric two-frame clear · safety-stream freshness fail-safe · safety CRC corruption fail-safe · recovery requires REARM · FIFO0 ring buffer · ESTOP via 0x011 SYS_SAFETY_STS · dedicated 0x204 watchdog · REARM-timeout stays inhibited · multi-stream readiness latch.

CI runs `pio test -e native` on `mtr-stm32/**` changes (`.github/workflows/ci.yml`). Legacy suites under `test/legacy/` remain as standalone flat executables but are excluded from the native env.

---

## 10. Core Algorithms (Pseudo-code)

Faithful abstractions of the production code — use for review; the source files remain authoritative.

### 10.1 Boot (`main.cpp`)

```
main():
    if RCC.CSR.IWDGRSTF:                    # capture reset cause BEFORE clearing
        iwdg_reset = true; RCC.CSR.RMVF = 1
    HAL_Init(); enable DWT cycle counter
    SystemClock_Config()                    # HSI 16 MHz, no PLL, flash WS 0
    motor.init()                            # relays -> OFF, DAC EEPROM <- 0 V
    if not can.init(): Error_Handler()      # disable IRQ + halt; outputs stay safe
    motor.set_diag(diag); can.set_diag(diag)
    if iwdg_reset: diag.raise(MtrWatchdogReset); diag.recover(MtrWatchdogReset)
    IWDG.init(prescaler=128, reload=400)    # ~32 kHz/128 = 250 Hz -> 1.6 s window
    loop forever:                           # see 10.2
```

### 10.2 Main Loop

```
loop:
    now = HAL_GetTick()
    while can.poll_rx(frame):               # every pass (~1 ms)
        motor.handle_frame(frame, now)
    if now - last_loop >= 5 ms:
        last_loop = now
        can.service_recovery()              # bus-off recovery + TX-health shadow
        motor.tick(now)                     # safety evaluation
        probe_tick(now)                     # no-op unless kTxProbe
        if kEnableCanBroadcast:             # diagnostics drain (gated)
            for up to 8: if diag.pop_pending(r): can.send(encode_0x631(r))
        if kEnableCanBroadcast:             # periodic telemetry (compiled out otherwise)
            every 10 ms: can.send(motor.build_throttle_status_frame())   # 0x120
            every 20 ms: can.send(motor.build_motor_feedback_frame())    # 0x206
            every 20 ms (+10 ms phase): can.send(encode_0x502(motor.fill_node_status()))
    IWDG.refresh()                          # ONLY after the full cycle above
    HAL_Delay(1)
```

### 10.3 RX Dispatch (`motor.handle_frame`)

```
handle_frame(frame, now):
    last_rx = now; first_frame_seen = true; comms_timed_out = false
    switch frame.id:
    case 0x001:                             # hardwired ESTOP broadcast
        trigger_estop()
    case 0x011:                             # persistent E-stop authority
        handle_safety_status(frame, now)    # see 10.5
    case 0x110:                             # mode authority
        decode; if not mode_val.observe(rc, now):
            if was_valid: on_authority_loss()
            break                           # keep last mode, inhibit drive
        current_mode = mode ? AUTO : MANUAL; ready |= MODE
    case 0x204:                             # drive command
        decode; if not mode_valid: diag(MtrCmdStreamUnauthorised); break
        target_speed, target_gear = cmd
        gap = drive_seen ? now - last_drive : 0; last_drive = now; drive_seen = true
        if gap > 100:   recover_count = 1
        elif recover_count < 3: recover_count++
        if not timed_out or recover_count >= 3:
            timed_out = false; ready |= DRIVE
    case 0x113:                             # power authority
        decode; if not pwr_val.observe(rc, now):
            if was_valid: on_authority_loss(); break
        pwr_on = power_state != 0
        if not pwr_on:              rearm_off_seen = true; ready &= ~POWER
        elif rearm_required and rearm_off_seen and mode_valid:
                                    rearm_observed = true; ready |= POWER
        elif rearm_required and not rearm_off_seen:
                                    diag(MtrRearmSequenceViolation); ready &= ~POWER
        else:                       ready |= POWER
        power_on = pwr_on
```

### 10.4 Periodic Evaluation (`motor.tick`)

```
tick(now):
    # 1. Generic comms deadman
    comms_timed_out = first_frame_seen and (now - last_rx > 500)
    if comms_timed_out: on_authority_loss()

    # 2. 0x011 freshness (checked every tick, not only on frame arrival)
    if safety_valid and now - last_safety > 700:
        safety_valid = false; safety_val.invalidate(); on_authority_loss()
        diag(MtrSysSafetyStsTimeout, now - last_safety)
    ignition = power_valid and power_on and safety_valid
               and (not rearm_required or rearm_observed)

    # 2b. Dedicated 0x204 drive watchdog
    expect_drive = mode_valid and power_valid and safety_valid and power_on
                   and current_mode == AUTO and (not rearm_required or rearm_observed)
    if expect_drive:
        ref = drive_seen ? last_drive : arm_time     # dead-sender case still trips
        if not timed_out and now - ref > 150:
            timed_out = true; recover_count = 0; ready &= ~DRIVE
            diag(MtrRtDriveCmdTimeout, now - arm_time)
    else:
        timed_out = false; arm_time = 0; recover_count = 0

    # 3. Fail-safe gate (single point of propulsion cutoff)
    if estop or comms_timed_out or timed_out or not power_valid or not safety_valid
       or (rearm_required and not rearm_observed) or not all_ready_bits:
        target_speed = 0; relays.set_state(OFF); dac.force_zero()
        if rearm pending and now - rearm_start >= 10 s: diag(MtrRearmSequenceViolation)
        return

    if not mode_valid: target_speed = 0          # keep power, zero propulsion

    # 4. D<->R shift arc protection
    if (active==D and target==R) or (active==R and target==D):
        if dwell not started: dwell = now; dac.force_zero(); relays.set_gear(N, ignition)
    if dwell started:
        if now - dwell < 50: dac.force_zero(); return
        dwell = 0
    active = target
    relays.set_gear(active, ignition)
    if not ignition: dac.force_zero(); return

    # 5. Throttle
    speed_mag =  target_speed  if active==D and target_speed > 0
    speed_mag = -target_speed  if active==R and target_speed < 0    # canonical negative
    speed_mag =  target_speed  if active==R and target_speed > 0    # legacy positive
    if neutral or speed_mag == 0: dac.force_zero()
    else: dac.set_throttle(dac_code(speed_mag), true)
```

### 10.5 E-stop Latch / Asymmetric Clear / REARM

```
trigger_estop():
    estop = true; targets = 0; relays.set_state(OFF); dac.force_zero()
    on_authority_loss(); clear_confirm = 0; last_zero = false   # stale zeros never pair

handle_safety_status(frame, now):            # 0x011, DLC 5
    decode; if crc8_h2f(data[0..3], 0x3C11) != e2e_crc:
        safety_val.invalidate(); safety_valid = false; clear state reset
        on_authority_loss(); diag(MtrSysSafetyCrcError); return
    if not safety_val.observe(rc, now):      # duplicate expiry / gap / reorder
        clear state reset; if was_valid: on_authority_loss()
        diag(MtrSysSafetyCounterStale); return
    last_safety = now
    if estop_source != 0:                    # assert frame
        if not estop: trigger_estop()
        clear_confirm = 0; last_zero = false; return
    # estop_source == 0: two-frame asymmetric clear
    if not estop:                            # healthy: observe only, no credit
        clear_confirm = 0; last_zero = true; clear_seq_rc = rc; return
    first = not last_zero
    advances = (rc == (clear_seq_rc + 1) mod 256)
    clear_confirm = 1 if (first or not advances) else clear_confirm + 1
    clear_seq_rc = rc; last_zero = true
    if clear_confirm >= 2: authorized_clear(now)

authorized_clear(now):
    estop = false; mode_valid = false; power_valid = false
    mode_val.invalidate(); pwr_val.invalidate()   # cached commands cannot re-enable motion
    clear state reset; rearm_required = true; rearm_observed = false
    # rearm_off_seen intentionally NOT reset: OFF edge observed during ESTOP carries over
    rearm_start = now; on_authority_loss()
```

### 10.6 Stream Validity (shared, per supervised stream)

```
observe(rc, now):                            # modulus 256, max forward delta 2
    event = counter_tracker.observe(rc)      # increment/wrap/gap/recovery/duplicate/reorder
    if not valid:                            # reacquiring: baseline keeps authority invalid
        valid = event in {increment, wrap, gap, recovery}; return valid
    if event == duplicate:                   # delta 0: reject, DO NOT refresh freshness
        if freshness expired: invalidate()
        return valid
    if event == reorder or not freshness_ok(now):   # delta > 2 or stale
        invalidate(); return false
    return true
```

### 10.7 Relay Controller

```
set_gear(gear, ignition):
    if not ignition: set_state(OFF); return
    D or S -> DRIVE; R -> REVERSE; N/default -> PARK

apply_state(s):                              # active-low: RESET = relay ON, SET = relay OFF
    DRIVE:   rev=SET;  ign=RESET; drive=RESET
    REVERSE: drive=SET; ign=RESET; rev=RESET      # drive/rev can never both be RESET
    PARK:    drive=SET, rev=SET; ign=RESET
    OFF:     all SET

set_state(s):
    if s != current: apply(s); if not kTxProbe: toggle PC6 LED
```

### 10.8 DAC Controller

```
set_throttle(code, enabled):
    target = 0 if (not enabled or code == 0) else clamp(code, 655, 1966)
    if target == 0: force_zero(); return
    if target != current and write_dac_raw(target): current = target

force_zero():
    if not eeprom_burned and write_dac_eeprom(0):    # one-time: hardware powers up at 0 V
        current = 0; return
    if current == 0:
        if ++zero_refresh >= 50: zero_refresh = 0; write_dac_raw(0)   # ~250 ms refresh
        return
    if write_dac_raw(0): current = 0

write_dac_raw(value, eeprom=false):
    if cached_addr and try_write(cached_addr): return true
    cached_addr = 0                                  # NACK invalidates cache
    for addr in {0x60, 0x61, 0x62} << 1:             # bit-banged I2C, ACK on 9th clock
        if try_write(addr): cached_addr = addr; return true
    return false
    # payload: [cmd (0x60 reg+EEPROM | 0x40 reg), value>>4, (value<<4)&0xF0]
    # EEPROM write waits 50 ms (t_prog)
```

### 10.9 CAN Driver

```
isr_fifo0():                                  # NVIC priority 5
    while GetRxMessage(FIFO0) ok:
        push frame into 32-slot ring; on full: rx_overflow++, diag(MtrFdcanRxOverflow)

poll_rx(out):
    disable IRQ; if empty: enable IRQ; return false
    out = ring[tail++]; enable IRQ; return true

service_recovery():                           # 5 ms; SINGLE PSR/ECR reader
    shadow.tec = ECR[7:0]; shadow.rec = ECR[14:8]
    shadow.lec = PSR[2:0]; shadow.bus_off = PSR.BO
    if not shadow.bus_off and State != ERROR: recovery_in_progress = false; return
    if recovery_in_progress and now - last < 150: return          # debounce
    recovery_in_progress = true; last = now
    if State == ERROR: State = READY; start()                     # repair HAL state
    else: clear CCCR.INIT                     # ISO 11898-1: 128 x 11 recessive bits
    diag(MtrFdcanBusOff, (tec << 8) | rec)
    re-arm FIFO0 notification
```

### 10.10 TX Diagnostic Probe (kTxProbe builds only)

```
probe_tick(now):
    render LED: SolidOn=steady, Slow=500/500 ms, Fast=100/100 ms, Dark=off
    if awaiting_sample:
        if now - sent >= 10 ms:              # shadow refreshed by service_recovery
            h = tx_health()
            if h.bus_off:                                   led = Dark
            elif not tx_pending() and h.lec in {0,7} and h.tec <= tec_at_send: led = SolidOn
            elif h.lec == 3:                                led = Slow   # ACK error
            else:                                           led = Fast   # bus-level error
            awaiting_sample = false
    elif now - last_send >= 1000 ms:
        last_send = now; tec_at_send = tx_health().tec
        if send(build_motor_feedback_frame()): awaiting_sample = true
        else: led = Fast                    # TX FIFO rejected
```

---

## 11. Parameter Reference

### 11.1 Configuration constants (`src/config.h`)

| Constant | Value | Meaning |
|---|---|---|
| `kCanBitrateHz` | 500 000 | Classic CAN Low bus |
| `kEnableCanBroadcast` | `false` | Receive-only mode: periodic TX and 0x631 drain compiled out |
| `kTxProbe` | `false` | Bench TX diagnostic probe (1 Hz 0x206 + PC6 LED verdict) |
| `kRelayRevPin` / `kRelayDrivePin` / `kRelayIgnitionPin` | PA0 / PA2 / PA4 | Active-low relay outputs |
| `kI2cSclPin` / `kI2cSdaPin` | PA5 / PA7 | Bit-banged I2C to MCP4725 (open-drain) |
| `kLedPin` | PC6 | Status LED, active-low |
| `kDacMinCode` / `kDacMaxCode` / `kDacZeroCode` | 655 / 1966 / 0 | 0.8 V / 2.4 V / 0.0 V @ 5.0 V ref |
| `kMaxForwardSpeedMmps` / `kMaxReverseSpeedMmps` | 3000 / 500 | Setpoint envelope (mm/s) |
| `kWatchdogTimeoutMs` | 500 | Generic any-frame comms deadman |
| `kFeedbackPeriodMs` / `kThrottlePeriodMs` | 20 / 10 | 0x206+0x502 / 0x120 periods (compiled out) |
| `kMainLoopPeriodMs` | 5 | Control tick |
| `kDriveCmdTimeoutMs` | 150 | Dedicated 0x204 watchdog (while drive expected) |
| `kDriveCmdRecoverFrames` / `kDriveCmdRecoverMaxGapMs` | 3 / 100 | Confirmed 0x204 recovery |

### 11.2 Supervisor and actuation constants

| Constant | Value | Source | Meaning |
|---|---|---|---|
| `kAuthFreshMs` | 500 | `motor_manager.h` | 0x110/0x113 stream freshness (5 × 100 ms cycle) |
| `kSafetyFreshMs` | 700 | `motor_manager.h` | 0x011 freshness (FTTI-derived, non-multiple of 200 ms) |
| `kRearmTimeoutMs` | 10 000 | `motor_manager.h` | Diagnostic timeout for unobserved REARM |
| `kShiftDwellMs` | 50 | `motor_manager.h` | D↔R neutral dwell |
| `kDacActiveFloor` | 700 | `motor_manager.h` | Motion floor ≈ 0.855 V (clears actuator deadband) |
| `MTR_READY_MODE/POWER/DRIVE` | bits 0/1/2 | `motor_manager.h` | Multi-stream readiness latch |
| `shared::kLowSpeedThreshMmps` | 50 | `shared_config.h` | Below this → DAC 0 (no idle-voltage jitter) |
| `shared::kMtrFault*` | 0x01/0x02/0x04/0x08/0x10 | `shared_config.h` | 0x206 fault flags (estop/timeout/adc/gear/ready) |

### 11.3 Driver, watchdog and probe parameters

| Parameter | Value | Source | Meaning |
|---|---|---|---|
| RX ring size | 32 frames | `can_driver.h` | FIFO0 ISR → main-loop handoff |
| Bus-off debounce | 150 ms | `can_driver.h` | `service_recovery()` retry interval |
| FDC1 IRQ priority | 5 | `can_driver.h` | Below HAL critical sections (priority 0) |
| IWDG prescaler / reload | 128 / 400 | `main.cpp` | ~32 kHz LSI → 250 Hz → ~1.6 s window |
| Diag drain budget | 8 frames / 5 ms tick | `main.cpp` | 0x631 burst limit (gated) |
| Probe send period / sample delay | 1000 ms / 10 ms | `main.cpp` | 1 Hz 0x206, verdict after shadow refresh |
| Probe LED periods | 500/500 ms, 100/100 ms | `main.cpp` | Slow (1 Hz) / Fast (5 Hz) patterns |
| DAC settle after power-up | 50 ms | `dac_controller.h` | ISO1540 + MCP4725 rail stabilization |
| DAC EEPROM t_prog wait | 50 ms | `dac_controller.h` | MCP4725 EEPROM programming time |
| DAC zero refresh | 50 × 5 ms = 250 ms | `dac_controller.h` | Keeps 0 V through brown-out/hot-plug |

### 11.4 Protocol frame parameters (generated contracts)

| Frame | ID | DLC | Cycle | Direction |
|---|---|---|---|---|
| `SAFETY_ESTOP` | 0x001 | 0 | event | RX |
| `SYS_SAFETY_STS` | 0x011 | 5 | 200 ms | RX |
| `SYS_MODE_CMD` | 0x110 | 2 | 100 ms | RX |
| `SYS_PWR_CMD` | 0x113 | 2 | 100 ms | RX |
| `RT_DRIVE_CMD` | 0x204 | 5 | 10 ms | RX |
| `SYS_THROTTLE_STS` | 0x120 | 2 | 10 ms | TX (gated) |
| `MTR_MOTOR_FBK` | 0x206 | 4 | 20 ms | TX (gated) |
| `MTR_NODE_STATUS` | 0x502 | 8 | 20 ms | TX (gated) |
| `MTR_DIAG_EVENT_RPT` | 0x631 | 8 | event | TX (gated) |


