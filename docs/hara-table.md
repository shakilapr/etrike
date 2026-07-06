# E-Trike HARA Table — Research Vehicle

> **Companion to** `tem/safety/02-hara.md` (authoritative ISO 26262 HARA).
> This document expands the 7 production-oriented hazards to ~30 hazards across 9 domains,
> uses research-appropriate RL (Research Level) scaling with dual-phase ratings,
> and incorporates the vehicle's operational design domain (ODD) as a research prototype.

---

## 1. Methodology

### 1.1 Research-Level (RL) Scale

Standard ISO 26262 ASIL ratings assume a production vehicle operated by the general public
in uncontrolled environments. This E-Trike is a **research vehicle** with phased testing
(testbed → ground → closed track → urban traffic, over years). A tailored scale is used:

| RL | ISO 26262 Equivalent | Meaning for Research Vehicle | Safety Rigor Required |
|----|----------------------|------------------------------|----------------------|
| **RL-1** | QM | No credible harm scenario in current phase | Standard quality processes |
| **RL-2** | ASIL A / low B | Minor harm possible; trained operator present | Documented mitigations, basic testing |
| **RL-3** | ASIL B / low C | Moderate harm possible; supervised environment | Independent monitoring, HIL testing |
| **RL-4** | ASIL C / D | Life-threatening harm possible | Full EGAS/ISO 26262 rigor, hardware redundancy |

The S/E/C → RL mapping follows ISO 26262-3:2018 Table 2, but the label is **RL** (not ASIL)
to avoid implying production certification. The RL level determines the minimum verification
rigor required before the vehicle enters that test phase.

### 1.2 Dual-Phase Rating

Each hazard is rated twice:

- **Current Phase**: The highest-risk phase the vehicle currently operates in
  (testbed → ground → closed track). Exposure and controllability are favorable
  (trained operators, supervised, low speed, controlled environment).
- **Target Phase**: Urban traffic in Sri Lanka (public roads, 40 km/h, general riders,
  3 passengers + driver). Exposure is high, controllability is reduced.

**Severity (S) is identical across phases** — the same physics produces the same harm
regardless of who is operating or where.

### 1.3 Test Phase Applicability Codes

| Code | Phase | Current? | Environment |
|------|-------|----------|-------------|
| **T** | Testbed | ✓ Now | Bench, no motion, CAN simulation |
| **G** | Ground | ✓ Soon | Open area, low speed (<10 km/h), supervised |
| **C** | Closed Track | Next | Controlled circuit, moderate speed, trained operator |
| **U** | Urban Traffic | Target | Public roads, Sri Lanka, 40 km/h max, general use |

### 1.4 ODD (Operational Design Domain) Reference

| Parameter | Testbed | Ground | Closed Track | Urban Traffic (Target) |
|-----------|---------|--------|-------------|------------------------|
| Max speed (adjustable clamp) | 0 km/h | <10 km/h | <25 km/h | 40 km/h (allowed max) |
| Road type | N/A | Flat ground | Carpet / paved | Carpet road |
| Weather | Indoor | Sunny | Sunny | Sunny, clear daytime |
| Slope | 0° | <5° | <15° | ≤30° |
| Passengers | 0 | 0 (riderless) | 1 (operator) | 3 + driver (4 total) |
| Load | 0 kg | 0 kg (no occupants) | 100 kg | 100 kg × 4 = 400 kg |
| Vehicle kerb weight | ~500 kg | ~500 kg | ~500 kg | ~500 kg |
| Vehicle gross weight | ~500 kg | ~500 kg | ~600 kg | ~900 kg |
| Mode | All | All | All | AUTO + MANUAL |
| Supervision | Engineer at bench | Remote observer + planned remote stop | Trained operator on vehicle | General rider |
| BMS regen | N/A | Unknown | Unknown / TBD | Must be confirmed |
| SEB comm-loss behavior | N/A | Datasheet available — verify | Datasheet + empirical test | Must be validated |

### 1.5 Rating Definitions

**Severity (S0–S3):**
| S | Description | E-Trike Example |
|---|-------------|-----------------|
| S0 | No injury | Minor discomfort |
| S1 | Minor to moderate injury | Bruising, abrasion from low-speed fall |
| S2 | Severe but not life-threatening | Fractures, hospitalization |
| S3 | Life-threatening or fatal | Rollover, pedestrian struck, collision at speed |

**Exposure (E0–E4):**
| E | Description | E-Trike Example |
|---|-------------|-----------------|
| E0 | Incredibly unlikely | — |
| E1 | Very low | Occurs < once per year in test phase |
| E2 | Low | Occurs a few times per year |
| E3 | Medium | Occurs monthly |
| E4 | High | Occurs every drive cycle |

**Controllability (C0–C3):**
| C | Description | E-Trike Example |
|---|-------------|-----------------|
| C0 | Controllable in general | Normal driving |
| C1 | Simply controllable | Rider can correct with normal inputs |
| C2 | Normally controllable | Rider can correct but requires attention/strength |
| C3 | Difficult to control | Rider cannot reliably prevent harm; requires ESTOP |

---

## 2. HARA Table

### 2.1 Propulsion Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-01** | Unintended acceleration | Uncommanded positive drive torque: corrupt CAN `0x204`, MTR software fault, DAC voltage above commanded | Vehicle stopped at crossing or launch; AUTO mode; unexpected motion into path of pedestrians or traffic | T/G/C/U | S3 | E1 | C2 | RL-2 | E4 | C3 | RL-4 | DAC=0V, gear=N, vehicle motionless | 200 ms | EGAS 3-level (MTR L1 + SYS L2 + HW L3); MTR 200ms `0x204` staleness; speed clamping [-500, 3000] mm/s; MCP4725 power-on pulldown to 0V | EGAS L2 speed mismatch >500 mm/s for 500 ms (SYS); MTR ESTOP GPIO ISR (<1ms); `0x204` DLC guard in `can_protocol.h` | Existing HE-01. At 40 km/h (target ODD), 200 ms covers 2.2 m. 3000 mm/s clamp = 10.8 km/h — **less than ODD 40 km/h max**. Clamp value may need review for urban phase. |
| **H-02** | Unintended reverse direction | Gear mismatch: MOSFET relay energizes R while D commanded, or corrupt CAN `0x204` gear byte | Vehicle stopped, AUTO mode, about to launch forward; vehicle moves backward into obstacle/person | T/G/C/U | S3 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | Gear=N, motor=0V | 200 ms | Gear conflict detection (multiple lines HIGH → forced N); speed-gated switching (<50 mm/s); TLP281 optoisolator gear sense (fail-safe: all HIGH = N) | MTR `GearControl::all_off()` on ESTOP; `kMtrFaultGearConflict` bit in `0x206` | ESTOP path covers this; no dedicated reverse-direction monitor beyond ESTOP. |
| **H-03** | DAC I2C failure → stuck throttle | MCP4725 I2C bus NACK; DAC retains last written value; motor continues at last commanded speed despite new commands | Any mode; MTR attempts to change throttle but DAC doesn't respond; vehicle speed uncontrolled | T/G/C/U | S3 | E1 | C2 | RL-2 | E3 | C3 | RL-4 | DAC=0V via HW ESTOP GPIO | <100 ms (HW) | MCP4725 finite I2C timeout (100 ms, never `HAL_MAX_DELAY`); one retry on failure; consecutive failure tracking | `Mcp4725Dac::write()` returns false; consecutive failures logged; HW ESTOP GPIO (L3) is primary backstop | Software ESTOP path may not clear DAC output. Documented in `docs/hardware-safety.md`. **Relies on HW GPIO for ultimate backstop.** |
| **H-04** | Gear relay stuck/welded under load | MOSFET driving 72V relay welds closed due to switching under load above 50 mm/s; gear cannot be changed | Vehicle at moderate speed (>50 mm/s) when gear change commanded; relay remains energized in previous gear | G/C/U | S3 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | Gear=N via second relay path or power-off | N/A | Speed-supervised gear switching (<50 mm/s); gear conflict detection; TLP281 sense feedback | Conflict detection forces N if multiple lines HIGH | **Rider must power off if relay welds.** No automatic remediation beyond conflict detection. Contactors rated for make/break under load need verification. |
| **H-05** | ADC throttle sensor stuck-at-rail | Throttle grip potentiometer shorts to GND (raw=0) or VCC (raw=4095); MANUAL mode reads corrupted ADC value | MANUAL mode; rider twisting grip but ADC returns fixed value; vehicle may not accelerate or may jump to full throttle | G/C/U | S2 | E2 | C2 | RL-2 | E3 | C2 | RL-3 | ADC fault flag set; in AUTO mode, CAN `0x204` overrides ADC | N/A | ADC raw=0 or raw=4095 detection in MTR; fault flag set in diagnostics | `kMtrFaultAdcFault` bit in `0x206`; raw=0 produces 0V (safe); raw=4095 produces max throttle | In MANUAL, raw=0 is safe (no motion). Raw=4095 is hazardous — **no automatic DAC cut on ADC stuck-high in MANUAL.** Relies on rider pressing ESTOP. |
| **H-06** | Motor overspeed beyond phase limit | Speed clamping set too high for current test phase; motor controller drives motor beyond the phase-appropriate safe speed | AUTO mode on downhill or open road; vehicle exceeds safe speed envelope for the current test phase | C/U | S3 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | Speed clamped to phase-appropriate limit; motor torque zeroed | 200 ms | Adjustable speed clamp per test phase (initial: very low; increases gradually with validation); EGAS L2 monitoring | EGAS L2 mismatch detection; `0x206` feedback speed vs `0x204` setpoint | Clamp is configurable, not fixed. Risk is that clamp is raised too aggressively without sufficient validation at each speed increment. **Phased speed increase protocol recommended** — each increment requires passing safety checks at current speed before raising. |

### 2.2 Braking Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-07** | Complete loss of braking | Brake demand not delivered: CAN `0x7B9` not transmitted, SEB comm fault, SYS crash, checksum failure, or SEB internal fault | Vehicle approaching stopped traffic, obstacle, or intersection; AUTO or MANUAL mode; brake demand fails to produce deceleration | T/G/C/U | S3 | E1 | C3 | RL-3 | E4 | C3 | RL-4 | SEB max stroke (27 mm), max pressure (5 MPa) | 220 ms | Mode-gated dual control (Option D): RT sends `0x7B9` in AUTO, SYS sends in MANUAL/ESTOP; RT brake takeover on SYS heartbeat loss (200 ms); BRAKE_DEGRADED state (lever-based, no sync needed); brake priority chain: ESTOP > lever > CAN > release | SYS heartbeat loss → RT takeover (200 ms); SEB internal comm watchdog (20 ms); `0x7B9` checksum + rolling counter; brake following-error monitor (3 mm, 100 ms) | **No mechanical brake backup** — entirely by-wire. If both SYS and RT fail simultaneously, no brake actuation path exists. SEB behavior during SYS watchdog reset (~2.5s window) is **empirically unverified** — documented in `issues/emergency-safety-analysis.md` Issue 2, `docs/emergency-system.md` §6. |
| **H-08** | Unintended / ghost braking | CAN `0x205` or `0x7B9` corruption → large kPa value; brake lever sensor stuck HIGH; SEB applies brake without demand | Any mode, any speed; vehicle decelerates unexpectedly; following traffic may collide | T/G/C/U | S2 | E1 | C1 | RL-1 | E3 | C2 | RL-3 | Brake released (0 mm stroke) via rider override or mode change | N/A | Brake priority chain; lever-based control in MANUAL; ESTOP brake is intentional (not ghost) | No direct "unintended brake" detection beyond lever stuck-high monitoring | Rider can power off (ignition) to resolve. No automatic release. Following-vehicle risk is moderate (brake light illuminates). |
| **H-09** | Degraded braking (partial loss) | SEB provides less than commanded pressure/stroke; hydraulic leak, air in lines, SEB motor degraded; CAN `0x7B9` partially corrupted | Vehicle braking from speed; deceleration is less than expected; stopping distance increases | G/C/U | S2 | E1 | C2 | RL-2 | E4 | C2 | RL-3 | Maximum available brake applied; ESTOP if critical | 220 ms | Brake following-error monitor (`0x7B9` cmd vs `0x721` fb, 3mm threshold, 100ms); SEB BRAKE_DEGRADED → ACTIVE recovery; `0x731` SEB_ErrInfo fault monitoring (14 L3 bits) | Following-error monitor; SEB internal self-diagnostics; `SEB_Error_Status` in `0x721` | Partial brake loss is not independently distinguishable from full loss at the CAN level if SEB reports plausible-but-wrong feedback. |
| **H-10** | SEB watchdog-reset brake gap | SYS external watchdog (TPS3850) fires during braking; SYS resets; CAN `0x7B9` stops; SEB enters internal comm-fault after ~20 ms | Vehicle braking in AUTO or MANUAL; SYS watchdog fires (deadlock, brownout); SEB behavior during SYS reboot uncertain | G/C/U | S3 | E1 | C3 | RL-3 | E3 | C3 | RL-4 | SEB holds last commanded pressure; or brake-hold relay maintains hydraulic pressure | 20 ms (SEB timeout) | RT brake takeover on SYS heartbeat loss (200 ms); external watchdog on separate IC (TPS3850); independent MTR motor kill on reset | SEB internal comm watchdog (20 ms); RT SYS heartbeat monitor (200 ms) | **CRITICAL GAP:** SEB behavior during comm-loss is empirically unverified — it may hold or release. If it releases: ~2.5s window with no brakes. Documented in `docs/emergency-system.md` §6, `issues/emergency-safety-analysis.md` Issue 3. **Hardware brake-hold relay recommended.** |
| **H-11** | Dual-sender CAN collision on `0x7B9` | RT and SYS both transmit `0x7B9` simultaneously; CAN arbitration produces corrupted frame or winner-takes-all with stale data | AUTO mode; SYS 6-condition suppression fails; RT and SYS both send `0x7B9` at 50 Hz | C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | One sender wins arbitration; frame is valid CAN (no corruption from two senders of same ID) | 20 ms (next frame) | 6-condition SYS suppression deadman: `AUTO && rt_hb_ok && rt_safety==Normal && seb_roll_ok && !lever && !estop && rt_sp_fresh`; any condition fails → SYS resumes; CAN arbitration guarantees one winner | Dual-sender detected as unexpected `0x7B9` on bus (both nodes listen); suppression logic tested in `test_safety_features.cpp` S19 | CAN arbitration prevents electrical collision; both frames are valid CAN. The risk is **stale data** from the non-authoritative sender, not electrical corruption. |
| **H-12** | Parking brake / hill-hold failure on slope | On 30° slope, SEB cannot maintain sufficient holding pressure after stop; vehicle rolls | Vehicle stopped on steep grade (30° per ODD); brake pressure bleeds off; rollback into traffic or obstacle | C/U | S3 | E0 | C2 | RL-1 | E3 | C3 | RL-4 | Vehicle remains stationary on grade | N/A | SEB max stroke (27 mm) at 5 MPa; ESTOP brake applied at max | No dedicated hill-hold function; SEB pressure is maintained by continuous `0x7B9` commands | **No parking pawl or mechanical parking brake.** 30° slope with 400 kg load may exceed SEB holding capacity. Must be validated. |

### 2.3 Steering Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-13** | Unintended steering angle | Steering angle command departs from intended value: corrupt CAN `0x169`, RT kinematics fault, EPS-C internal fault, dynamic clamp bypass | AUTO mode; vehicle cornering or lane-keeping at 5–40 km/h; uncommanded steering change causes path departure | T/G/C/U | S3 | E1 | C3 | RL-3 | E4 | C3 | RL-4 | Steering centered (0°) or silent-stop; vehicle stopped | 500 ms | Dynamic angle clamp (speed-dependent, 5°–40°); software hard-stops (±40°); `0x169` XOR checksum + rolling counter; EPS-C internal dual-sensor angle validation; steering following-error monitor | Following-error: `max(2°, 0.25 × dynamic_limit)` for >300 ms → ESTOP; EPS-C L3 fault via `0x202 SES_ErrInfo`; EPS-C internal comm watchdog (20 ms) | Delta tricycle rollover threshold ~0.5g lateral. At 40 km/h (ODD max), dynamic clamp is ~5°. **Rollover risk increases with speed.** No mechanical steering column — rider cannot override EPS-C. Documented in HE-03. |
| **H-14** | Steering mechanical jam during ESTOP ramp | Linkage jammed by rock, bent tie rod, or debris; ESTOP centering ramp (20°/s) encounters persistent following error >1s | ESTOP triggered; RT attempts to ramp steering to 0° but linkage is mechanically blocked; EPS-C motor fights the jam | G/C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | Silent-stop (stop `0x169`); STEER_FAULT; EPS-C internal comm-fault hold | 1000 ms (ramp timeout) | Mechanical jam fallback: persistent following error >1s during ramp → silent-stop → STEER_FAULT; START button recovery → STEER_LISTEN_SYNC; START long-press (3s) → force-activate at 0° | Following-error monitor active during ramp; jam detection at 1s persistence | Recovery from STEER_FAULT after jam requires rider action (START short/long press). If EPS-C remains fault-locked, power-cycle is final recovery. |
| **H-15** | Angle sensor offset drift (undetected) | EPS-C angle sensor calibration drifts due to temperature, vibration, or wear; reported angle ≠ true angle; steering commands produce wrong road-wheel angle | AUTO mode; vehicle tracks straight according to `0x201` feedback but actual road-wheel angle is offset; gradual path drift | C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | STEER_FAULT on boot if offset >30°; ESTOP if following-error detected | 500 ms (if detected) | Boot-time angle alignment check (>30° → FAULT); following-error monitor at runtime | Boot sync: `SES_INF_Angle_Status != 1` or |angle| > 30° → STEER_FAULT | **No continuous drift monitoring during operation.** Boot check catches catastrophic offset; gradual drift within 30° during a drive goes undetected. Periodic recalibration recommended for urban phase. |
| **H-16** | Dynamic angle clamp failure → rollover | Clamp formula bug, software bypass, or RT crash; steering angle exceeds speed-dependent safe limit; delta tricycle exceeds rollover threshold | AUTO mode, high speed (25–40 km/h); Jetson commands or kinematics produces large angle; clamp fails to limit it | C/U | S3 | E1 | C3 | RL-3 | E3 | C3 | RL-4 | Vehicle within stable envelope; steering angle ≤ dynamic limit | 500 ms (following-error to ESTOP) | Dynamic angle clamp: `limit = 40.0 − (v_kmh − 2.0) × (35.0/23.0)`, clamped [5.0, 40.0]; software hard-stops ±40°; hardware end-stops ~±40° (mechanical) | Following-error monitor (speed-scaled); EPS-C internal angle monitoring | **Single point of failure for rollover prevention.** Clamp is computed in RT `physics_resolve()` — no independent second computation or SYS-side verification. At 40 km/h, clamp is ~5°; bypass → full lock → rollover. |
| **H-17** | EPS-C L3 dual-sensor fault | Both primary and secondary angle sensors fail (open/out-of-range); or both torque sensors T1/T2 fail; EPS-C cannot determine steering angle | AUTO or MANUAL mode; EPS-C internal redundancy exhausted; steering feedback is invalid | T/G/C/U | S3 | E1 | C3 | RL-3 | E3 | C3 | RL-4 | ESTOP: motor killed, brake max, steering silent-stop | <50 ms (from `0x202` to ESTOP) | EPS-C dual-redundant angle sensors (primary + secondary); dual torque sensors (T1 + T2); `0x202 SES_ErrInfo` reports 8 L3 fault bits | `0x202` processed by RT; L3 bits → immediate ESTOP via CAN `0x001`; tested in `test_safety_features.cpp` S12 | EPS-C is factory-preprogrammed, not user-serviceable. L3 faults are terminal — unit must be replaced. |

### 2.4 ESTOP / Emergency Stop Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-18** | ESTOP fails to stop vehicle | ESTOP command (button, CAN `0x001`, heartbeat timeout) fails: motor continues producing torque, SEB fails to apply brake, or both | Any mode, any speed; emergency situation requiring immediate halt; ESTOP is the highest-priority safety function | T/G/C/U | S3 | E1 | C3 | RL-3 | E3 | C3 | RL-4 | All motion inhibited: DAC=0V, gear=N, SEB max stroke, steering safe | 100 ms (HW path) | Three independent ESTOP paths: (1) HW: button → MTR PA1 ISR → DAC=0 + relays OFF (<10 ms), (2) SW: button → SYS GPIO → CAN `0x001` → all nodes, (3) Soft: heartbeat timeout → controlled stop; rate-limited ESTOP (2 per 500ms); ESTOP as absorbing state (no CAN exit) | HW ESTOP GPIO (NC, active-low, dual-wired); ESTOP button continuity test on startup; diag task ESTOP duration monitor (>30s → flag) | **No physical brake fallback.** If CAN bus is dead *and* SYS is crashed, ESTOP kills motor but cannot actuate SEB. Vehicle coasts without active braking. The HW ESTOP path (L3) kills motor reliably; brake actuation depends on SYS or RT being functional. |
| **H-19** | False / spurious ESTOP | Noise on ESTOP button GPIO, CAN `0x001` glitch, transient heartbeat timeout, or sensor bounce triggers ESTOP without real hazard | Vehicle in motion; unexpected full stop in traffic; following vehicles may not react; occupants may be thrown forward | G/C/U | S2 | E1 | C1 | RL-1 | E3 | C2 | RL-3 | ESTOP activates (safe state); rider exits via START button → MANUAL | N/A (ESTOP is the safe state) | NC wiring (broken wire → ESTOP, not "everything fine"); threshold + duration debounce on all fault checks (300–1500 ms); frozen counter detection prevents single-bit CAN glitch | Debounce on all ESTOP triggers (300ms following error, 500ms command stale, 1000-1500ms heartbeat); rate limiting on CAN `0x001` | False ESTOP is inherently safe but disruptive. Service disruption in urban traffic could cause rear-end collision. Rider education: press START to recover, check surroundings first. |
| **H-20** | ESTOP CAN flood / DoS | Corrupted node continuously broadcasts `0x001` (or close variant); bus saturated; legitimate messages delayed or dropped | Any mode; corrupted node floods CAN bus with ESTOP or high-priority frames; actuator commands cannot reach EPS-C/SEB/MTR | G/C/U | S2 | E1 | C2 | RL-2 | E2 | C2 | RL-2 | Rate-limited ESTOP processing; corrupted node isolated | N/A | ESTOP rate limiting (2 frames per 500ms window per node); `0x001` highest CAN priority; bus-off detection + auto-recovery; sender-ID tracking for diagnostics | `shared::should_send_estop_now()` rate limiter; CAN TEC/REC error counters; bus-off isolation | Rate limiting prevents ESTOP flood but does not protect against non-ESTOP CAN flood from corrupted node. Bus-off detection isolates the node after TEC > 255. During the window before bus-off, other messages may be delayed. |
| **H-21** | MTR ESTOP ACK timeout | ESTOP triggered; MTR does not set `ESTOP_ACTIVE` fault flag in `0x206` within 100 ms; CAN ACK path unconfirmed | ESTOP event; HW GPIO path kills motor, but CAN ACK path times out; SYS cannot confirm MTR received ESTOP | T/G/C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | HW GPIO path is primary kill; CAN ACK is secondary confirmation | 100 ms (ACK timeout) | HW ESTOP GPIO wired direct to MTR (L3, <10 ms); SYS monitors `0x206` for ESTOP_ACTIVE bit; retrigger ESTOP on ACK timeout | `kMtrFaultEstopActive` bit in `0x206` verified by SYS within 100 ms; retrigger + persistent fault on timeout | **HW GPIO path is the primary kill — ACK is diagnostic only.** No safety gap if HW path works. ACK failure indicates CAN path issue, logged for investigation. Tested in `test_safety_features.cpp` S13. |

### 2.5 Communication Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-22** | Low CAN bus-off → all actuators lost | TWAI TX error counter exceeds 255; persistent bus-off (5 consecutive detections); node cannot transmit on low bus | Any mode; RT, SYS, MTR, or PWT enters bus-off; actuator commands (steering, brake, motor) cannot reach their targets | T/G/C/U | S3 | E1 | C3 | RL-3 | E3 | C3 | RL-4 | All nodes detect bus-off independently; ESTOP if persistent; MTR autonomous stop on `0x204` staleness | 200 ms (`0x204` staleness) | CAN bus-off detection at 1 Hz (SYS) and 10 Hz (RT); 5 consecutive detections → ESTOP; auto-recovery attempted (128 × 11 recessive bits); per-link independent heartbeats | TEC/REC counters via TWAI driver; `can_health.h` monitor on RT; SYS bus-off monitoring in `task_diag` | Low bus carries all actuator commands. **Bus-off is non-survivable** — vehicle must ESTOP. Dual-redundant CAN buses (two physical buses) would mitigate but add cost/complexity. |
| **H-23** | High CAN bus-off → Jetson disconnected | MCP2515 SPI or CAN errors; Jetson cannot send `0x300`/`0x301` or receive telemetry; RT loses planning commands | AUTO mode; Jetson disconnected from RT; vehicle loses autonomous driving capability | T/G/C/U | S2 | E2 | C1 | RL-2 | E3 | C1 | RL-2 | Zero setpoints, steering ramp-to-zero; MANUAL mode transition | 500 ms (`0x300` staleness) | `0x300` command staleness watchdog (500 ms); Jetson heartbeat `0x7FC` timeout (1500 ms); assisted stop (2000 kPa brake); MANUAL mode fallback | `can_health.h` monitor on RT (interrupt + polled); MCP2515 error flags; `0x7FC` alive counter frozen detection | Survivable — Jetson is QM, not safety-critical. Vehicle continues in MANUAL mode. Rider maintains full control. |
| **H-24** | CAN message corruption undetected | Bit errors on bus that pass checksum (statistically improbable for XOR but possible with multi-bit errors); wrong data accepted as valid | Any CAN message; corrupted data enters control loop; actuator receives incorrect command | G/C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | EPS-C/SEB internal checksum rejects corrupt frames; MTR DLC guard rejects wrong-length frames | 20 ms (next valid frame) | EPS-C/SEB: XOR checksum + rolling counter on every frame; 3 consecutive rejections → actuator internal fault; MTR: DLC guard on all `from_frame()` methods; ECG: rolling counter freshness checks | Checksum XOR byte 0–6 ^ 0xFF; rolling counter 4-bit (0–15) monotonic; DLC validation in `can_protocol.h` | XOR is not a CRC — multi-bit errors can produce valid XOR. AUTOSAR E2E Profile 1 (CRC-8 + counter) would be stronger. Documented in `standards/autosar-e2e-protection.md`. **Update path identified but not yet implemented.** |
| **H-25** | Gateway forwarding failure (RT or PWT) | RT stops forwarding `0x001`/`0x011`/`0x302` between buses; or PWT stops forwarding `0x001`/`0x012`; important frames do not reach their destination bus | ESTOP event or mode change; gateway drops frames; target nodes do not receive critical information | T/G/C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | Heartbeat timeout detects node crash; ESTOP broadcast reaches all nodes on originating bus | 200–1500 ms (heartbeat) | ESTOP `0x001` forwarded transparently (bypasses queue); RT dual-bus heartbeat; per-link independent timeouts | Heartbeat timeout on peer node; `0x001` is highest priority and should always win arbitration | Gateway is a single point of failure for cross-bus communication. If RT crashes, no cross-bus forwarding. SYS and RT are separate MCUs → independent failure modes → tolerable. |
| **H-26** | CAN RX queue overflow (burst traffic) | High CAN bus load or corrupted node floods bus; RX queue (depth 16) overflows; legitimate frames dropped | Bus under heavy load or attack; safety-critical frames may be dropped along with non-critical ones | G/C/U | S2 | E1 | C2 | RL-2 | E2 | C2 | RL-2 | ESTOP uses `xQueueSendToFront` for priority delivery; non-critical frames dropped first | N/A | ESTOP frames get `xQueueSendToFront` (priority delivery); overflow counter incremented and logged; rate limiting on ESTOP broadcast | Queue overflow counter in diagnostics; task health monitoring | Queue depth 16 is small for high bus load. Under sustained flood, even priority frames may eventually be dropped if queue is full of ESTOP frames. |

### 2.6 Power & Electrical Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-27** | 72V isolation failure / short to chassis | Traction battery positive shorts to vehicle chassis through wiring damage or insulation failure; 72V present on touchable surfaces | Any mode; wiring insulation compromised (vibration, pinch, heat); rider or passenger contacts energized chassis | T/G/C/U | S3 | E1 | C3 | RL-3 | E3 | C3 | RL-4 | 1A fuses blow; TVS clamps transient; chassis isolated | <1 ms (fuse) | 4kV galvanic isolation via TLP281 optocouplers on all gear sense lines; 1A fuses on 72V branch circuits; SMCJ90CA TVS diodes; dedicated HV isolation design (`docs/high-voltage-isolation.md`) | Fuse blows on overcurrent; TVS clamps to 146V; TLP281 blocks 72V from 3.3V domain | 72V is hazardous DC voltage. Isolation failure is a life-safety issue independent of vehicle motion. **Periodic insulation resistance testing (megger) recommended before each test phase.** |
| **H-28** | 12V rail failure → CAN transceivers dead | DC-DC converter fails or 12V rail shorts; all CAN transceivers (SN65HVD230) lose power; all CAN buses go silent | Vehicle in motion; 12V rail drops → CAN transceivers off → all nodes detect bus-silent → heartbeat timeouts fire everywhere | G/C/U | S3 | E1 | C2 | RL-2 | E3 | C3 | RL-4 | Each MCU enters safe state on heartbeat timeout; MTR kills motor locally (HW ESTOP GPIO independent of CAN) | 200 ms (SYS hb) – 1000 ms (RT hb) | DC-DC stays ON in ESTOP (maintains 12V for safety); independent power domains (72V → motor, 12V → logic, 3.3V → MCUs); TPS3850 external WDT on each MCU | All peer heartbeats stop → each node times out independently → all nodes enter safe state | **12V is single point of failure for all CAN communication.** No redundant 12V supply. If DC-DC fails, all CAN buses go silent simultaneously. HW ESTOP GPIO (L3) on MTR is the only non-CAN safety path remaining. |
| **H-29** | DC-DC converter failure in ESTOP | DC-DC converter fails; 12V rail lost; SYS/RT/MTR MCUs lose power; all CAN transceivers dead | ESTOP in progress; DC-DC fails → MCUs power down → motor controller sees 0V (DAC loses power → pulldown to 0V); SEB enters comm-loss fault | G/C/U | S2 | E1 | C2 | RL-2 | E3 | C3 | RL-4 | Motor: 0V (safe). Brake: uncertain (SEB comm-fault behavior). Gear: all relays de-energize → N (safe). | <100 ms (power loss → DAC=0V) | DC-DC explicitly kept ON in ESTOP (`0x012` enable = 1); TPS3850 external WDT resets MCUs on power sag; power-on defaults safe (DAC=0V, relays OFF) | MCU brownout detector; external WDT timeout; motor controller sees 0V on DAC power loss | Motor path is safe on power loss (DAC=0V, relays OFF). **Brake path is uncertain** — SEB behavior on complete power loss is not documented. Hydraulic pressure may bleed off or hold. |
| **H-30** | Load dump / regen voltage spike | Motor controller regenerates during braking or downhill; voltage spike on 72V rail; exceeds component ratings | Braking from speed or descending 30° slope; motor regen dumps energy to 72V rail; voltage spike propagates | C/U | S1 | E1 | C2 | RL-1 | E3 | C2 | RL-3 | TVS clamps to safe voltage; fuses protect downstream | <1 μs (TVS) | SMCJ90CA TVS diodes on all 72V lines (clamp 146V); 1A fuses; dedicated HV protection | TVS clamping; fuse blowing on sustained overvoltage | Regen on 30° slope with 400 kg load may produce significant energy. **Battery BMS must handle regen current** — BMS is out of scope per item definition but is a dependency. |
| **H-31** | Brownout causing MCU reset during motion | 12V rail sags below MCU minimum voltage; ESP32-S3 brownout detector triggers reset; MCU reboots during vehicle motion | Vehicle in motion; 12V rail sags (high load, DC-DC transient, battery sag); one or more MCUs reset | G/C/U | S2 | E1 | C2 | RL-2 | E3 | C3 | RL-4 | MCU reboots → MANUAL mode (safe default); DAC=0V during reset; gear relays OFF during reset | <100 ms (WDT) + 500 ms (reboot) + 3000 ms (grace period) | TPS3850 external WDT on separate power domain; power-on defaults safe (DAC=0V, relays OFF); MANUAL default mode; 3000 ms startup grace period suppresses false ESTOP | MCU brownout detector; external WDT timeout; DAC pulldown to 0V during reset | ~3.5s window (WDT + reboot + grace period) where safety monitoring is degraded. During this window, vehicle coasts (motor 0V, gear N). **Brake behavior during MCU reset depends on SEB comm-fault behavior** — same gap as H-10. |
| **H-32** | Reverse polarity / ESD damage | 12V input reversed (jump-start error) or ESD strike to exposed connector; component damage → undefined behavior | Maintenance or jump-start; connector handling; vehicle stationary but subsequent operation affected | T | S1 | E0 | C1 | RL-1 | E1 | C1 | RL-1 | Series Schottky blocks reverse current; TVS clamps ESD | N/A | Series Schottky diode on ESP32 12V input (reverse polarity); TVS diodes on exposed lines; TLP281 4kV galvanic isolation | Schottky blocks reverse current (no damage); TVS diodes clamp ESD to safe voltage | Stationary fault — only relevant during maintenance. Mitigations are standard. |

### 2.7 Autonomous Function Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-33** | Jetson perception missed obstacle (false negative) | LiDAR/camera fails to detect pedestrian, vehicle, or obstacle; no `0x400` obstacle distance or `UINT32_MAX` sent; RT obstacle limiting not triggered | AUTO mode; obstacle in vehicle path; Jetson perception stack misses it (sensor failure, algorithm error, occlusion, adverse lighting) | C/U | S3 | E1 | C3 | RL-3 | E4 | C3 | RL-4 | Vehicle stops via ESTOP or rider intervention | N/A (no automatic detection without sensor) | RT obstacle limiting: speed 0→full between 300–3000 mm; brake 0→5000 kPa between 3000–300 mm; obstacle ESTOP at ≤300 mm + speed >50 mm/s; dynamic angle clamp (rollover protection under emergency braking) | Jetson is primary sensor; some redundancy planned (ultrasonic/radar) | **CRITICAL GAP:** Jetson perception is QM-rated and is currently the sole obstacle sensor. Redundant sensors (ultrasonic, radar) are planned but not yet integrated. Until redundant obstacle detection is operational, this hazard severity remains S3/C3. For urban phase, independent ASIL-rated obstacle detection is required. |
| **H-34** | Jetson planning sends dangerous maneuver | Planning algorithm outputs extreme speed, yaw rate, or gear command due to software bug, bad map data, sensor misclassification, or adversarial input | AUTO mode; Jetson publishes dangerous `/cmd_vel`; RT receives via CAN `0x300` and `0x301` | T/G/C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | Speed clamped, steering clamped, brake bounded | 100 ms (RT control loop) | RT speed clamping [-500, 3000] mm/s; dynamic angle clamp (5°–40°); brake KPA bounded to [0, 5000]; obstacle limiting overrides planning speed | RT `physics_resolve()` clamps all Jetson outputs before actuator commands | RT is the safety barrier between QM Jetson and safety-critical actuators. All planning commands are bounded. However, **within the bounded envelope, a malicious or buggy planner can still cause hazardous behavior** (e.g., swerving within 5° at 40 km/h). |
| **H-35** | Mode split-brain (SYS and RT disagree) | SYS `g_mode` ≠ RT `g_mode`; SYS broadcasts `0x110` MANUAL but RT thinks it's AUTO (or vice versa); actuator control sources mismatch | After CAN glitch, SYS mode change, or race condition; one node in AUTO, other in MANUAL; dual-sender collision on `0x7B9` or both nodes silent | T/G/C/U | S2 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | SYS is authoritative; SYS periodically refreshes `0x110` (1s interval); mode-gated actuator control: MTR follows `0x110`, brake uses SYS suppression deadman, steering uses RT local mode | 1000 ms (SYS mode refresh) | SYS is authoritative mode source; `0x110` broadcast on every change + 1s periodic refresh; MTR slave to `0x110`; 6-condition SYS brake suppression deadman; `0x110` DLC=1 with mode enum | SYS 1s periodic refresh prevents split-brain persisting >1s; ESTOP overrides any mode disagreement | Mode split-brain is bounded to max 1s by periodic refresh. During this window, the 6-condition deadman will fail (SYS thinks MANUAL → suppresses `0x7B9`) and RT may also suppress (thinks AUTO but SYS heartbeat still OK). **Tested in simulation** (`mode-transition-can.test.ts`). |

### 2.8 Slope-Specific Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-36** | Rollback on 30° slope from stop | Vehicle stopped on steep grade; insufficient holding torque or brake pressure; vehicle rolls backward downhill | AUTO mode; vehicle stopped on 30° slope (ODD max) at ~900 kg gross; motor torque or SEB holding pressure insufficient; rollback into traffic/obstacle below | C/U | S3 | E0 | C3 | RL-1 | E3 | C3 | RL-4 | Vehicle remains stationary; brake holds; motor provides holding torque | N/A (depends on detection) | SEB max brake (27 mm, 5 MPa); motor controller holding current capability | No dedicated hill-start assist or rollback detection | **No hill-hold function exists in current firmware.** Motor controller holding torque at 0 speed is unknown. SEB holding pressure on 30° with ~900 kg gross (500 kg kerb + 400 kg payload) is unvalidated. For urban phase with 30° slopes, hill-hold assist is recommended. |
| **H-37** | Overspeed on descent (gravity-assisted) | Vehicle descending 30° slope; gravity adds to motor speed; speed exceeds ODD limit or dynamic clamp threshold; rollover risk increases | AUTO mode; descending steep grade at ~900 kg gross; motor regen may be insufficient to maintain speed limit; vehicle accelerates beyond safe envelope | C/U | S3 | E0 | C2 | RL-1 | E3 | C3 | RL-4 | Speed clamped; motor regen braking active; SEB braking if overspeed persists | 200 ms (EGAS L2) | Speed clamping; EGAS L2 speed monitoring; dynamic angle clamp reduces with speed; obstacle limiting provides additional braking if needed | EGAS L2 mismatch detection (speed > setpoint + 500 mm/s for 500 ms → ESTOP); `0x206` feedback speed monitoring | **BMS regen capability is unknown.** On 30° descent with ~900 kg, gravitational acceleration component is ~4.9 m/s². If regen is unavailable, all braking is friction (SEB) — risk of brake fade on long descent. No grade-aware speed control exists. Brake may be needed to maintain speed. |
| **H-38** | Insufficient braking authority on steep grade | SEB maximum braking force (5 MPa → caliper clamping force) insufficient to decelerate ~900 kg on 30° descent; stopping distance significantly increased | AUTO or MANUAL mode; emergency braking on steep descent; SEB at max pressure but vehicle does not stop within expected distance | C/U | S3 | E0 | C3 | RL-1 | E3 | C3 | RL-4 | Maximum available braking; ESTOP; motor regen supplements braking (if available) | N/A (SEB physical limit) | SEB max pressure 5 MPa; ESTOP applies max brake; motor regen provides additional deceleration (if BMS supports it) | Brake following-error monitor detects SEB stroke but cannot detect insufficient hydraulic force | **SEB braking capacity on 30° slope with ~900 kg gross has not been validated.** Brake system was selected for flat-ground operation. SEB datasheet is available — rated force should be checked against 30° requirement. BMS regen unknown — if unavailable, all braking is friction-only, increasing fade risk on long descents. |
| **H-39** | Slope-induced lateral instability | Vehicle traversing across slope (camber); 30° lateral slope + steering input → delta tricycle tilts; inside wheel lifts → rollover | AUTO or MANUAL mode; vehicle traversing across a 30° slope or encountering cambered road at ~900 kg gross; combined with steering input → rollover threshold exceeded (delta tricycle: a_y/g > T/(2h), higher CG with passengers worsens margin) | C/U | S3 | E0 | C3 | RL-1 | E3 | C3 | RL-4 | Dynamic angle clamp limits steering; ESTOP if rollover imminent | N/A (no rollover sensor) | Dynamic angle clamp inversely proportional to speed; software hard-stops ±40°; delta tricycle rollover threshold: a_y/g > T/(2h) | No IMU or tilt sensor; rollover detection is purely kinematic (speed + angle model) | **No IMU/accelerometer for rollover detection.** Relies entirely on kinematic model + angle clamp. On cambered road, the effective rollover threshold is reduced. At ~900 kg gross with passengers, CG height increases — rollover threshold decreases. For urban phase, an IMU-based rollover warning/protection is recommended. |

### 2.9 Mode Management & HMI Hazards

| ID | Hazard | Malfunctioning Behavior | Operational Scenario | Phase | S | E cur | C cur | RL cur | E tgt | C tgt | RL tgt | Safe State | FTTI | Existing Mitigations | Detection | Gaps / Notes |
|----|--------|------------------------|----------------------|-------|---|-------|-------|--------|-------|-------|--------|------------|------|----------------------|-----------|-------------|
| **H-40** | Uncommanded AUTO engagement | System transitions from MANUAL to AUTO without rider intent: MODE button glitch, CAN `0x110` corruption, or SYS mode state machine fault | MANUAL mode; rider in direct control; sudden transition to AUTO → Jetson commands take over steering, throttle, brake without rider expectation | G/C/U | S3 | E1 | C2 | RL-2 | E3 | C2 | RL-3 | ESTOP via button; MANUAL via MODE button; throttle/gear pass-through in MANUAL | 500 ms (mode debounce) | MANUAL→AUTO requires steering aligned + heartbeats valid (gated transition); ESTOP button overrides any mode; SYS authoritative mode source; 500 ms debounce on MODE button | `mode_manager.cpp`: transition gated by `SES_INF_Angle_Status == 1` + heartbeats; ESTOP button preempts | If AUTO engages with steering not aligned → steering snap at EPS-C sync. Debounce prevents glitch. Gated transition prevents unaligned entry. |
| **H-41** | ESTOP exit during hazardous condition | Rider presses START button to exit ESTOP while hazardous condition still exists (e.g., obstacle still present, steering still jammed); vehicle resumes motion into danger | ESTOP state; hazard that triggered ESTOP is still present; rider exits ESTOP and vehicle moves | C/U | S2 | E2 | C2 | RL-2 | E3 | C3 | RL-4 | Deferred steering ramp completion; MANUAL mode (rider in control); power-cycle ultimate fallback | N/A | ESTOP exit always → MANUAL (never AUTO); deferred steering ramp (centering completes before handoff); START button health monitoring (>30s ESTOP → diag flag); MODE long-press (3s) secondary exit | ESTOP duration monitor; rider education (dashboard indicators) | **Human factors risk:** Rider may exit ESTOP prematurely without checking surroundings. Training and clear indicators (brake light ON in ESTOP) mitigate. No technical prevention of premature exit — this is intentional (rider must not be trapped in ESTOP). |
| **H-42** | Mode indicator failure (both bulbs OFF) | Both AUTO and MANUAL mode bulbs fail; rider cannot distinguish ESTOP (both OFF + brake light ON) from power-off (all lights OFF including brake); mode confusion | Any mode; both mode bulbs failed; rider sees dark dashboard; may not know if vehicle is in ESTOP, MANUAL, or powered off | G/C/U | S1 | E2 | C1 | RL-1 | E4 | C1 | RL-1 | Brake light ON in ESTOP provides independent indication; vehicle behavior observable | N/A | Brake light ON during ESTOP (powered from always-on DC-DC rail, independent of accessory relay); brake light OR-logic (lever OR ESTOP OR CAN OR SEB stroke); rider can feel motor/brake state | `0x011 SYS_SAFETY_STS` reports light state to RT/Jetson; diag task monitors outputs | QM function per existing safety case. Brake light provides fail-visible distinction from power-off. Mode bulbs are incandescent (simple, reliable) — dual-filament or LED with current monitoring recommended for urban phase. |
| **H-43** | Brake light failure during braking | Brake light fails to illuminate while vehicle is braking; following traffic unaware of deceleration | Any mode; vehicle braking (lever, ESTOP, AUTO CAN, or SEB stroke >0.5mm); brake light bulb or wiring failed | G/C/U | S2 | E1 | C2 | RL-2 | E4 | C2 | RL-3 | Brake light OR-logic provides redundancy; brake still works (fail-visible where possible, fail-safe for braking function) | N/A | Brake light OR-logic: lever OR ESTOP OR CAN brake bit OR SEB stroke >0.5mm; powered from always-on DC-DC rail (not accessory relay) | No dedicated brake light failure detection (bulb monitoring) | QM function for the braking system itself (brake still works). **Rear-end collision risk** from failed brake light is real. Bulb current monitoring or dual-filament bulb recommended for urban phase. |

---

## 3. Summary Statistics

### 3.1 Hazard Count by Domain

| Domain | Hazards | IDs |
|--------|---------|-----|
| Propulsion | 6 | H-01 – H-06 |
| Braking | 6 | H-07 – H-12 |
| Steering | 5 | H-13 – H-17 |
| ESTOP | 4 | H-18 – H-21 |
| Communication | 5 | H-22 – H-26 |
| Power & Electrical | 6 | H-27 – H-32 |
| Autonomous Functions | 3 | H-33 – H-35 |
| Slope-Specific | 4 | H-36 – H-39 |
| Mode Management & HMI | 4 | H-40 – H-43 |
| **Total** | **43** | |

### 3.2 RL Distribution — Current Phase

| RL | Count | Hazards |
|----|-------|---------|
| RL-1 (QM) | 9 | H-08, H-12, H-19, H-30, H-32, H-36, H-37, H-38, H-39, H-42 |
| RL-2 (ASIL A/B) | 25 | H-01–H-06, H-09, H-11, H-14, H-15, H-20, H-21, H-23–H-26, H-28, H-29, H-31, H-34, H-35, H-40, H-41, H-43 |
| RL-3 (ASIL B/C) | 9 | H-07, H-10, H-13, H-16, H-17, H-18, H-22, H-27, H-33 |
| RL-4 (ASIL C/D) | 0 | *(No RL-4 hazards in current test phases)* |
| **Total** | **43** | |

### 3.3 RL Distribution — Target Phase (Urban Traffic)

| RL | Count | Hazards |
|----|-------|---------|
| RL-1 (QM) | 3 | H-32, H-42 |
| RL-2 (ASIL A/B) | 7 | H-20, H-23, H-25, H-26, H-30 |
| RL-3 (ASIL B/C) | 18 | H-02, H-04, H-05, H-06, H-08, H-09, H-11, H-14, H-15, H-19, H-21, H-24, H-28, H-29, H-31, H-34, H-35, H-40, H-43 |
| RL-4 (ASIL C/D) | 15 | H-01, H-03, H-07, H-10, H-12, H-13, H-16, H-17, H-18, H-22, H-27, H-33, H-36, H-37, H-38, H-39, H-41 |
| **Total** | **43** | |

### 3.4 RL Escalation from Current to Target

| Escalation | Count | Significance |
|------------|-------|-------------|
| RL-1 → RL-3 | 4 | Slope hazards become critical in urban phase (H-36, H-37, H-38, H-39) |
| RL-1 → RL-4 | 2 | H-12 (hill-hold failure), H-41 (ESTOP exit during hazard) |
| RL-2 → RL-3 | 12 | Most propulsion, braking, comm, power hazards escalate one level |
| RL-2 → RL-4 | 7 | H-01, H-03, H-07, H-10, H-13, H-16, H-18, H-22, H-27, H-33 escalate to RL-4 |
| RL-3 → RL-4 | 6 | H-07, H-10, H-13, H-16, H-18, H-22 escalate to RL-4 |
| No change | 10 | H-20, H-23, H-25, H-26, H-30, H-32, H-35, H-42 (or minimal escalation) |

**Key insight:** 0 hazards are RL-4 in current phases, but **15 hazards** become RL-4 in the urban traffic target phase. This validates the phased approach — the vehicle is safe for current supervised testing but requires significant safety upgrades before urban deployment.

---

## 4. Gap Priority List

Ranked by (target RL × severity × test-phase proximity):

### Critical (Must Resolve Before Ground Testing / Riderless)

| # | Hazard | Gap | Recommended Action |
|---|--------|-----|--------------------|
| **G1** | H-10: SEB watchdog-reset brake gap | SEB comm-loss behavior documented in datasheet but not empirically verified on the actual unit. ~2.5s window with potential no-brake if SEB releases on comm-loss. | **Verify SEB datasheet spec for comm-loss behavior.** Empirically test on bench. Install brake-hold relay gated by TPS3850 RST line as defense-in-depth. |
| **G2** | H-07: No mechanical brake backup | Entirely by-wire; if both SYS and CAN bus fail, no brake actuation. | Validate RT brake takeover path (HIL test S2). Consider supplementary direct-wired brake relay. |
| **G3** | H-16: Dynamic angle clamp → single point of failure | Rollover prevention relies on one software clamp in RT with no independent verification. With ~900 kg gross and higher CG, rollover threshold is reduced. | Add SYS-side independent angle clamp validation using `0x169` monitoring. |
| **G4** | H-18/19: No remote kill switch implemented | Remote stop is planned but not yet built. Without it, ESTOP requires reaching the physical button — impossible during riderless testing. | **Implement remote kill switch before any riderless ground testing.** This is a gate requirement. |

### High (Should Resolve Before Closed Track)

| # | Hazard | Gap | Recommended Action |
|---|--------|-----|--------------------|
| **G5** | H-33: Perception redundancy planned but not integrated | Jetson perception is QM and currently sole obstacle sensor. Redundant sensors planned but not yet operational. | Integrate and validate redundant obstacle sensors (ultrasonic/radar) before closed track AUTO mode testing. |
| **G6** | H-06: Phased speed increase protocol needed | Speed clamp is adjustable but no formal protocol for when/how to raise it. Risk of raising too aggressively. | Define phased speed increase protocol: each increment requires passing all safety checks at current speed before raising. |
| **G7** | H-24: XOR checksum ≠ CRC | Multi-bit CAN errors can produce valid XOR; AUTOSAR E2E Profile 1 would be stronger. | Implement AUTOSAR E2E Profile 1 (CRC-8 + counter) on safety-critical frames (`0x169`, `0x7B9`, `0x204`). |
| **G8** | H-27: No periodic HV isolation testing | 72V isolation failure is life-safety; no scheduled testing. | Institute megger testing before each test phase and periodic inspection schedule. |

### High (Should Resolve Before Urban Traffic)

| # | Hazard | Gap | Recommended Action |
|---|--------|-----|--------------------|
| **G9** | H-36–H-39: Slope hazards completely unvalidated | No testing on any slope; ~900 kg gross weight at 30° is unvalidated for rollback, overspeed, braking, and lateral stability. BMS regen capability unknown — all braking may be friction-only. | **Confirm BMS regen capability.** Slope testing on closed track with incremental grade/load before any urban deployment. Validate SEB rated force against 30° requirement using datasheet. |
| **G10** | H-38: SEB braking capacity on 30° not validated | SEB datasheet available — rated force must be checked against ~900 kg on 30° descent. If insufficient, stopping distance may be unacceptable. | Review SEB datasheet for rated clamping force. Calculate stopping distance on 30° at max gross weight. Empirical validation on closed track. |
| **G11** | H-15: No continuous angle sensor drift monitoring | Gradual drift during operation undetected; boot check only catches >30° offset. | Add periodic angle plausibility check during operation (cross-reference with IMU or odometry). |
| **G12** | H-39: No IMU for rollover detection | Relies on kinematic model; no direct tilt measurement. With ~900 kg gross, CG is higher, rollover threshold lower. | Add IMU for rollover warning and independent verification of dynamic angle clamp. |

### Medium (Should Address Before Production)

| # | Hazard | Gap | Recommended Action |
|---|--------|-----|--------------------|
| **G13** | H-05: ADC stuck-high in MANUAL | No automatic DAC cut; relies on rider pressing ESTOP. | Add ADC plausibility check → automatic DAC=0V on stuck-high detection. |
| **G14** | H-28: 12V single point of failure | All CAN transceivers on one 12V rail; DC-DC failure kills all communication. | Consider redundant 12V supply or backup battery for CAN transceivers on safety-critical bus. |
| **G15** | H-37: BMS regen capability unknown | If regen unavailable, all braking on long descents is friction-only → risk of brake fade at ~900 kg gross. | Confirm BMS regen support. If unavailable, add brake temperature monitoring or descent speed limiting. |

---

## 5. Cross-References

### 5.1 Existing Safety Documents

| Document | Covers |
|----------|--------|
| `tem/safety/02-hara.md` | Authoritative ISO 26262 HARA (7 hazards, ASIL D ratings) |
| `tem/safety/03-safety-goals.md` | 7 safety goals with FTTI budgets |
| `tem/safety/04-functional-safety-concept.md` | EGAS 3-level, safety mechanisms mapped to goals |
| `tem/safety/05-technical-safety-concept.md` | Architecture-level requirements, FFI, CAN isolation |
| `tem/safety/README.md` | Safety case index, ASIL summary, certification readiness |
| `docs/fmea-brake-steer-motor.md` | 15 failure modes across brake (5), steering (5), motor (5) |
| `docs/defense-in-depth-safety.md` | 8 (now 9) independent safety layers |
| `docs/hardware-safety.md` | Component fail-safe behavior at power-up/reset |
| `docs/emergency-system.md` | Complete ESTOP system, 8 safety layers, emergency response matrix |
| `docs/hil-safety-test-plan.md` | 19 HIL test scenarios (all currently "Not tested") |
| `docs/traceability-matrix.md` | Requirements traceability HARA → goals → requirements → implementation → tests |
| `issues/emergency-safety-analysis.md` | 14 emergency safety issues with causal analysis |

### 5.2 Code References

| File | Relevant Content |
|------|-----------------|
| `shared/shared_config.h` | All timing constants, speed limits, brake limits, fault flags |
| `sys-esp32/src/safety_monitor.h` | SYS-side safety monitor (ESTOP, heartbeat, EGAS L2) |
| `sys-esp32/src/mode_manager.cpp` | Mode state machine with debounce, ESTOP exit logic |
| `sys-esp32/src/brake_control.h` | SEB brake control with 6-condition suppression |
| `rt-esp32/src/safety_monitor.h` | RT safety checks, safety event queue, ESTOP handling |
| `rt-esp32/src/steering_control.h` | 6-state steering machine with ESTOP ramp |
| `rt-esp32/src/can_health.h` | CAN bus-off detection and recovery |
| `rt-esp32/src/watchdog.h` | Command staleness watchdog (500ms) |
| `rt-esp32/src/heartbeat.h` | Dual heartbeat (independent per bus) |
| `mtr-stm32/src/main.cpp` | MTR control with ESTOP GPIO, 200ms staleness |
| `mtr-stm32/src/gear_control.h` | Gear MOSFET control with conflict detection |
| `mtr-stm32/src/mcp4725_dac.h` | DAC driver with finite I2C timeout |
| `shared/can/can_protocol.h` | DLC guards, XOR checksums, rolling counters |
| `native-test/test_safety_features.cpp` | 20 safety feature tests (S1–S20) |
| `native-test/test_estop_latch.cpp` | ESTOP latch behavior tests |

### 5.3 Standards References

| Standard | Document |
|----------|---------|
| ISO 26262-3:2018 | `standards/iso-26262-functional-safety.md` |
| ISO 11898 (CAN) | `standards/iso-11898-can.md` |
| AUTOSAR E2E | `standards/autosar-e2e-protection.md` |

---

## 6. Traceability Note

This document is a **companion** to the existing ISO 26262 safety case at `tem/safety/`.
It does not replace or contradict the authoritative HARA in `tem/safety/02-hara.md`.
Instead, it:

1. **Expands** the 7 hazardous events to 43 hazards covering all domains
2. **Adds** dual-phase RL ratings that reflect the research vehicle's phased development
3. **Incorporates** the project's stated ODD (40 km/h, 30° slope, 4 passengers, sunny daytime)
4. **Flags** gaps that must be resolved before each test phase
5. **Provides** a single reference table for hazard identification during design reviews

For formal safety case submissions, use `tem/safety/02-hara.md` (ISO 26262 methodology).
For day-to-day engineering decisions and phase-gate reviews, use this document.

---

*Generated: 2026-07-05 | Updated: 2026-07-05 (team clarifications incorporated) | Sources: 12 project documents, 15 code files, 117 native tests, 332 simulation tests*

**Updates from team clarification (2026-07-05):**
- Vehicle weight: ~500 kg kerb, ~900 kg gross (previously unstated)
- Speed clamps: adjustable per phase, not fixed at 10.8 km/h
- Test progression: bench → riderless + remote stop (planned) → operator inside → passengers
- BMS regen: unknown (flagged as gap G15)
- SEB datasheet: available (G1 updated — comm-loss behavior can be verified from documentation)
- Perception: redundancy planned but not yet integrated (G5 updated)
- Remote kill switch: planned but not implemented (G4 added as critical gate for riderless testing)
