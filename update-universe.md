# E-Trike → Autoware Universe — Plain Plan

## What this is

We are connecting the E-Trike to **Autoware Universe**, the current ROS 2 self-driving
software stack (we were previously aimed at the older "Autoware.Auto"). The goal is simple:
the trike should drive itself under Universe.

The existing safety architecture and CAN layouts remain compatible. The bridge needs the
largest rewrite, while Phase 2 adds two messages and corresponding RT/tooling work. SYS and
MTR firmware stay unchanged.

---

## Plain glossary

| Term | Means |
|---|---|
| **Bridge** | The Jetson program that translates Universe messages ↔ trike wired commands. |
| **RT / SYS / MTR** | The trike's three small onboard computers: RT = motion control, SYS = safety & mode authority, MTR = motor driver. |
| **Wired bus (CAN)** | The cables carrying commands and sensor readings between the computers and the steering/brake motors. |
| **Message / topic** | How Universe sends a command, e.g. "steer left 5°", "go". |
| **Mode** | Who is driving: a human (**MANUAL**) or the computer (**AUTONOMOUS**). |
| **Engage** | Autoware's software permission to transmit motion commands. In the pinned Universe stack, `/api/autoware/get/engage` is a topic and `/api/autoware/set/engage` is a service. Physical mode changes use the existing `0x111` path through SYS. |
| **ESTOP** | Emergency stop — cut motion immediately. |
| **PARK** | Hold the brake while stopped. |
| **Frame / signal** | One wired message / one value inside it. |

---

## The short version

1. **The bridge needs rewriting** for the pinned Autoware Universe interfaces.
2. The Phase 2 protocol, RT, toolkit, and simulation work is also **required**, including
   steering while stopped and accurate motion feedback.
3. Mode (who drives) and emergency stop are **safety decisions owned by SYS**, the trike's
   safety computer. The bridge only relays; it never decides these.
4. HMI and Jetson/Autoware may both request AUTO or MANUAL using the existing `0x111` command.
   SYS remains the sole authority that accepts/rejects the request and broadcasts confirmed mode.

---

## What changes, what doesn't

| Component | Change? | In one line |
|---|---|---|
| **Bridge (Jetson)** | **YES — required** | Rewrite for Universe types; fix signs, gears, `0x111` mode requests, engage, lights/hazards, emergency, lifecycle, parameters, and safety gating. Preserve `0x112` transport without treating it as working ignition control. |
| **Wired commands (protocol)** | **YES — additive/metadata only** | Keep every existing layout, document HMI and Host as permitted `0x111` producers, and add required `0x303` steering command and `0x121` motion report. |
| **RT computer** | **YES — narrowly scoped** | Add `0x303` direct steering-angle input and `0x121` motion feedback. Preserve everything else, including PID, encoders, resolvers, diagnostics, bypass flags, safety logic, and the existing `0x111` gateway. |
| **SYS computer** | **NO** | Already handles mode and emergency correctly; the bridge uses its existing path. |
| **MTR computer** | **NO** | Existing propulsion, gear, feedback, timeout, and ESTOP behavior stays unchanged. |
| **control-toolkit** | **YES — required** | Regenerate codecs; retain `0x111`, `0x112`, lights, and ESTOP controls; add `0x303` and decode `0x121`; exercise them under bench safety gating. |
| **simulation / native tests / debug-tool / vt-console** | **YES — required** | Regenerate and update models, fixtures, decoders, displays, and regression tests for `0x303` and `0x121`. |
| **Autoware settings** | **YES — small** | Lower speed/steer limits to trike size; keep mode timeout. |
| **New ROS packages** | **YES** | 4 small packages: bridge, protocol wrapper, launch, vehicle description. |

---

## The 6 things the bridge MUST fix

These six bridge corrections are required, followed by the mandatory Phase 2 work.

1. **Build for Universe types.** The old bridge used message fields that no longer exist in
   Universe. Port it to the current `Control` message and remove the deleted `VehicleKinematicState`.
   (Without this it will not even compile.)

2. **Flip the steering direction.** The trike's internals measure steering as "right = positive",
   but Universe uses "left = positive". If unfixed, the trike steers the **opposite** way.
   Fix: negate the steering value when sending and when reading.

3. **Use the correct gear numbers.** Universe's "DRIVE" is the number **2**; the old code used
   **1** (which Universe reads as NEUTRAL). If unfixed, a "drive" command is ignored and the
   trike **never moves**. Fix: translate the numbers both ways from the message definitions.

4. **Implement the control-mode service over the existing SYS-authorized path.** Universe calls
   `/control/control_mode_request` as an `autoware_vehicle_msgs/srv/ControlModeCommand`
   service. For AUTONOMOUS or MANUAL, the bridge sends `0x111` with the requested mode and a
   valid rolling counter. SYS accepts or rejects it using its existing ESTOP/mode rules. The
   service reports whether the request was admitted, while `/vehicle/status/control_mode`
   changes only after `0x210` confirms SYS's result. Unsupported partial-control modes return
   `success=false`.

5. **Use the pinned engage and command interfaces with matching delivery settings.** The
   bridge may subscribe to `/api/autoware/get/engage` through a launch remap; it must not treat
   the `/api/autoware/set/engage` service as a topic. The five `/control/command/*` outputs are
   reliable, transient-local, depth 1 in the pinned stack, so bridge subscriptions must be
   compatible. Motion output still requires engage plus confirmed SYS AUTO mode.

6. **React to the emergency signal.** Universe sends an emergency flag (true/false). On true,
   the bridge must send the stop command. (See "Emergency stop" below for the important
   safety rule about clearing it.)

These bridge corrections establish the Universe interface. Completion also requires the
mandatory Phase 2 RT/protocol/tooling work and all validation stages below.

---

## How Universe commands map to the trike

The Universe control pipeline flows: **trajectory_follower** (MPC/PID) → **shift_decider**
(gear) → **vehicle_cmd_gate** (safety gate: rate limits, engage check, emergency, pause) →
**bridge** (our code) → trike wired commands. The bridge is the last piece — everything
upstream is standard Autoware.

**Universe sends → bridge does → trike receives**

| Universe sends | Bridge action | Trike wired command |
|---|---|---|
| `Control` (steer angle + speed) | convert, flip steering sign | drive command (speed + yaw) |
| `GearCommand` (DRIVE/REVERSE/PARK) | translate numbers; PARK → brake hold | gear in drive command; brake hold for PARK |
| Turn / hazard lights | pass through | light command |
| Emergency (true) | send stop | ESTOP event |
| HMI or Autoware requests AUTO/MANUAL | bridge sends/relays existing `0x111` with rolling counter | RT forwards it; SYS decides and broadcasts confirmed mode |

**Universe expects back ← bridge builds from trike feedback**

| Universe expects | From trike feedback |
|---|---|
| Speed report | speed message (`0x120`) |
| Steering report | steering-angle message (`0x310`), sign flipped |
| Gear report | motor feedback (`0x206`) |
| Mode report | RT status (`0x210`) → mapped to autonomous |
| Turn / hazard reports | SYS safety status (`0x011`) |

---

## Steering direction

The trike's steering motor is wired so that a **positive number means turn right**. Universe
says a positive number means **turn left**. This is just a sign convention.

- The trike already measures and reports steering this way, so the bridge must flip the sign
  **both** when sending a command (right→left) and when reading the feedback (left→right).
- This is a one-line change in the bridge and carries no wiring risk.

## Gear numbers

Universe and the trike use different numbers for the same gears:

| Gear | Universe number | Trike number (wire) |
|---|---|---|
| NEUTRAL | 1 | **0** |
| DRIVE | **2** | 1 |
| REVERSE | 20 | 3 |

The old bridge hard-coded DRIVE as `1`, which Universe reads as NEUTRAL → the trike would
never move. Fix: read the gear value from Universe's message definition (DRIVE = 2) and
translate to the trike's value when sending, and back when reporting.

## Mode (who is driving) — requests from HMI or Autoware, authority in SYS

**HMI and Jetson/Autoware may request mode changes. SYS alone decides the physical mode.**

- HMI may send `0x111 HMI_MODE_REQ` directly or through Jetson.
- Autoware may also request AUTONOMOUS or MANUAL through
  `/control/control_mode_request`; the bridge translates that request to the same existing
  `0x111` CAN command and maintains the rolling counter.
- RT forwards `0x111` from High CAN to SYS on Low CAN.
- SYS applies its existing rules, including rejecting mode changes during ESTOP, and remains
  the sole authority that broadcasts confirmed mode.
- Existing `0x210 RT_STATE_RPT` carries the confirmed result back to the bridge. The bridge
  never reports AUTONOMOUS merely because it transmitted a request.
- Motion output requires both confirmed SYS AUTO mode and Autoware engage permission.

No SYS firmware or new mode-message ID is needed; the plan reuses `0x111`.

## HMI and operator paths — inputs, outputs, and feedback

Do not treat all HMI-related signals as commands from Jetson. Some are physical inputs to SYS,
some are CAN inputs to RT/SYS, some are physical outputs driven by SYS, and others are CAN
feedback from the units:

| Function | Source → destination | Kind | Universe/Jetson work |
|---|---|---|---|
| AUTO/MANUAL request | HMI or Bridge → High `0x111` → RT gateway → SYS | CAN input to SYS | Bridge translates supported Autoware control-mode requests to `0x111` and maintains the rolling counter. |
| Confirmed physical mode | SYS → Low `0x110` → RT/MTR; RT → High `0x210` → Bridge | Unit output/feedback | Bridge waits for `0x210` before publishing the confirmed Autoware control-mode report. It never treats transmitted `0x111` as confirmation. |
| Physical MODE button | Button GPIO → SYS | Physical input to SYS | No bridge emulation is required; preserve it as another request source handled by SYS. |
| Power/start request | HMI or tooling → High `0x112` → RT gateway → SYS | CAN input to SYS | Preserve codec, 1 Hz rolling counter, gateway, and tests. SYS currently only decodes/logs it, so do not claim a power-state output or ignition action. |
| Physical START button | Button GPIO → SYS | Physical input to SYS | Used for ESTOP recovery. Never replace it with `0x112` or an Autoware command. |
| Requested turn/head/brake lamps | Bridge/Host → High `0x302` → RT gateway → SYS | CAN input to SYS | Bridge sets requested left/right and brake-light bits. Preserve headlight support; stock Universe supplies no standard headlight command. |
| Physical turn/head switches | Switch GPIOs → SYS | Physical inputs to SYS | SYS combines these with `0x302`; the bridge does not read or emulate the GPIOs. |
| Actual lamps | SYS light arbitration → lamp GPIOs | Physical outputs from SYS | SYS remains the output owner. Host requests do not directly drive lamp GPIOs. |
| Actual lamp feedback | SYS → `0x011` → RT/Bridge | CAN output from SYS | Bridge builds turn and hazard reports from actual `light_left/right`, not from the last `0x302` request. Brake/head bits remain diagnostics/tooling feedback. |
| Physical brake lever | Lever GPIO/ADC → SYS | Physical input to SYS | SYS uses it for manual braking and brake-lamp arbitration. `0x600.brake_engaged` reports the lever state; it is not brake-actuator confirmation. |
| Autoware brake request | Bridge → High `0x301` → RT → Low `0x205`/brake path | CAN input to RT | Separate from the physical lever. Existing RT/SYS brake arbitration remains authoritative. |
| ESTOP assertion | Physical ESTOP → SYS, or any permitted CAN sender → `0x001` → all units | Physical/CAN safety input | Bridge sends `0x001` only for `emergency=true`; all existing unit reactions stay unchanged. |
| ESTOP state feedback | SYS `0x011` and RT `0x210` → Bridge | CAN output from units | Bridge reports disengaged/safe state until the units confirm recovery. |
| ESTOP reset | Physical START or documented physical MODE long-press → SYS | Physical input to SYS | Never generate from Autoware, Jetson, `0x112`, or another CAN command. |

Thus Jetson produces requested mode, light, brake, and ESTOP CAN inputs; it consumes confirmed
mode, actual-light, safety, and diagnostic outputs. SYS owns physical inputs, lamp outputs,
mode confirmation, brake arbitration, and ESTOP recovery. `0x112` is preserved as an input but
has no implemented power output/state feedback today.

## Emergency stop — physical reset by design

On an emergency signal (`true`), the bridge sends the stop command (`0x001`). That is
correct and required.

Clearing the emergency stop, however, is **intentionally only possible by a physical button
on the trike** (the START button, or a long-press of the mode button). This is a *safety
feature*, not a limitation: you do not want software able to cancel a hardware emergency
stop, because that would defeat the interlock.

Therefore, on an emergency signal of `false`, the bridge should **stop asserting** the stop
and then watch the trike's safety status: while the trike still reports "stopped", the bridge
reports the disengaged state and does not drive. It waits for a human to physically reset.
The bridge must **never** try to clear the hardware stop over the wire.

## PARK (hold the brake at rest)

Universe does command PARK. The bridge handles this **without any firmware change**:

- On a PARK command, the bridge sends a **brake-hold** command (`0x301`) and reports PARK
  back to Universe from its own memory.
- Since the trike has no separate "PARK" gear value, the bridge sends NEUTRAL on the gear
  field plus the held brake — functionally identical to park-hold. RT and SYS execute the
  brake path; MTR handles propulsion and gear only.

---

## The one firmware change that truly matters: steering while parked

**Why it is needed.** Today the trike's drive command carries only a *yaw rate* (how fast the
vehicle is turning), not a *steering angle* (which way the wheels point). Yaw rate is
meaningless at zero speed, so at a standstill the steering command is discarded — the wheels
stay centered and the trike cannot pre-position them. Universe, however, expects the wheel
angle to be tracked even when stopped (for pull-out, parking, and engaging from rest).

What currently happens at zero speed with only a yaw rate: **nothing** — the command is thrown
away by both the bridge and RT.

**Why it fails today.** The current Autoware.Auto bridge already converts
`AckermannControlCommand` steering angle to yaw rate using
`yaw = speed × tan(angle) / wheelbase`. Its `steering_to_yaw()` explicitly returns zero below
the configured 0.05 m/s threshold. RT independently applies its own absolute-speed 0.05 m/s
threshold in `PhysicsModel::resolve()` before performing inverse kinematics. At zero speed the
angle is therefore lost on the legacy `0x300` path and RT decays steering toward center.

The current bridge check is written as `speed < threshold`, not `abs(speed) < threshold`, so
it also zeros yaw for every negative/reverse speed. The Universe port must correct this to a
symmetric absolute-speed check and add forward/reverse boundary tests.

(Note: RT does have a fallback — if it *did* receive a non-zero yaw at standstill, it would
turn the wheel to full lock in that direction, preparing for a turn. But the angle→yaw
conversion never produces a non-zero yaw at v = 0, so this path is never reached from a
precise angle command.)

**The fix (one RT firmware change).** Add a new message that carries the actual steering
**angle** (not yaw rate). RT forwards that angle straight to the steering motor, which
already accepts an angle at any speed. Bypass the low-speed steering decay when this angle
message is present.

- The steering motor (SES) natively accepts a target angle regardless of speed, so no motor
  change is needed.
- The wheels then turn while parked; actual turning of the vehicle still only happens once
  the trike rolls (turn rate = speed × angle).

This RT change and the motion-report work below are required parts of the completed system.
SYS and MTR firmware remain unchanged.

---

## Phase 2 — required protocol, RT, tooling, and simulation work

These are mandatory after the bridge port is operational on `vcan`. They use new CAN IDs rather
than widening existing layouts, because strict-DLC decoders may reject changed in-service
frames. `0x303` extends the Host→RT command path and `0x121` extends RT→Host feedback. Neither
message changes SYS or MTR authority.

### 2.1 Steering angle at any speed — `0x303` (required)

Lets the trike steer while parked (see "The one firmware change that truly matters").

- **Protocol:** add `0x303 HOST_STEER_CMD` carrying a signed steering angle in 0.1-degree
  units (`int16`, ±450 for ±45°, right = positive), plus validity/freshness information.
- **RT firmware:** when `0x303` arrives, forward the angle to the steering motor and
  **bypass the low-speed steering cutoff** while it is fresh and valid. Define a timeout and
  make the direct-angle path authoritative so it cannot fight the legacy `0x300` yaw path.
- **Bridge:** send `0x303` from Universe's steering angle; keep sending the existing drive
  command for speed. For the legacy/compatibility yaw field in `0x300`, explicitly implement
  `abs(speed) < low_speed_threshold → yaw_rate = 0`; do not rely on RT's independent guard.
  This guard applies only to derived yaw rate—fresh valid `0x303` steering angle must continue
  through zero speed so standstill steering works.
- **Why safe:** new message ID; old firmware ignores it.

### 2.2 Motion report — `0x121` (required)

Gives Universe an honest turn rate and the real gear state.

- **Protocol:** add `0x121 RT_MOTION_RPT` carrying coherent measured speed, estimated yaw rate,
  physical gear, and validity/freshness flags. PARK is not a physical MTR gear.
- **RT firmware:** publish `0x121` from fresh measured speed and steering angle; compute
  `yaw_rate = velocity × tan(steering_angle) / wheelbase` with documented signs and units.
- **Bridge:** use `0x121` for `VelocityReport.longitudinal_velocity`, `heading_rate`, and
  physical gear reporting.

### 2.3 RT scope boundary — preserve existing features

The Universe migration is not an RT cleanup project. Do not remove or redesign code merely
because the production profile currently disables it or because it is not used by this plan.

- Keep the current PID controller, PID telemetry, calculated-speed estimator, encoder support,
  `DirectResolver`, bicycle `PhysicsModel`, diagnostics, bench modes, bypass variables, and
  feature flags.
- Keep the production vehicle configuration with `ETRIKE_RT_PID_MODE=0`, speed feedback set to
  `None`, and encoders disabled unless a separate validated hardware decision changes it.
- PID disabled means **normal open-loop longitudinal drive**: RT sends the bounded requested
  speed to MTR without adding PID correction. It does not mean stop, fault, or inhibit motion.
- Shadow and active PID behavior remain available for their existing bench/future profiles and
  are outside the Universe migration scope.
- Do not optimize away calculated-speed/PID work, suppress `0x220`, remove unused variables, or
  alter build variants as part of this plan. Such cleanup requires a separate review and tests.
- Limit functional RT edits to decoding/arbitrating fresh `0x303`, commanding steering at
  standstill, publishing `0x121`, updating CAN routing/filter tables where required, and adding
  corresponding tests.

### 2.4 Why these are safe to add

Existing frame layouts remain unchanged. Old firmware ignores the new IDs, but a completed
deployment requires the new RT firmware. Mixed versions must remain motion-disabled until the
bridge verifies the expected feedback/capability. Measure added CAN load and arbitration.

### 2.5 Orphans kept

Not used by stock Universe, but kept for tooling/future use: obstacle distance, PID
telemetry, HMI power request, headlight bits, brake/diagnostic reports, alternate resolver,
encoder support, calculated-speed estimator, and bench/bypass support. None are removed.

---

## The control_toolkit and the bridge — parallel paths, not chained

The **control_toolkit** (our bench engineering tool) talks **directly to the CAN bus** via a
USB adapter — it does NOT go through the bridge. It can replace the Jetson/Host role during
bench testing by sending the same CAN messages the bridge would (mode request, drive command,
direct steering, heartbeat, ESTOP). The toolkit may send `0x111` with the same rolling-counter
rules when mode testing is explicitly armed and propulsion is safely inhibited. In production,
the bridge talks to Autoware + CAN; in bench testing,
the control_toolkit talks to CAN directly. They are parallel paths, not chained.

The control_toolkit uses the same generated protocol codecs. Its mode, command, ESTOP, gear,
and new `0x303` behavior provide bench coverage for the bridge path.

---

## Rollout

**Phase 1 — bridge port and Autoware integration:**
1. Rewrite the bridge for the pinned Universe message, service, topic, and QoS interfaces.
2. Add the bridge, exported protocol, E-Trike launch, and vehicle-description ROS packages;
   load parameters and ensure lifecycle activation works.
3. Implement the two-layer gate: HMI or Autoware requests mode through `0x111`, SYS confirms
   physical mode, and engage enables or suppresses Host motion transmission.
4. Add bridge unit tests and `vcan` integration tests.

**Phase 2 — required RT/protocol/tooling work:**
5. Add `0x303` and `0x121` contracts, golden vectors, and regenerated codecs.
6. Implement only direct standstill steering and coherent motion reporting in RT; preserve
   PID-disabled open-loop driving and all existing optional/dormant RT features.
7. Update control-toolkit, simulation, native tests, debug-tool, and vt-console as applicable.
   Preserve HMI coverage for periodic `0x111`/`0x112` counters, lights, and ESTOP; tests must
   distinguish the currently effective `0x111` mode path from non-functional `0x112` power action.
   Add bridge/RT boundary cases at `-0.05`, just below `-0.05`, zero, just below `+0.05`, and
   `+0.05` m/s: legacy yaw uses a symmetric absolute-speed guard while fresh `0x303` steering
   remains active at every speed.

**Phase 3 — staged validation:**
8. Validate HMI-direct, HMI-via-Jetson, Autoware-service, and toolkit mode requests through RT
   to SYS. Confirm rolling counters, ESTOP rejection, confirmed `0x210` feedback, and no motion
   before SYS AUTO plus engage. Also verify `0x112` is transported without claiming that it
   switches vehicle power, and verify turn/hazard/headlight/brake-light mappings. Run injected
   mode and ESTOP tests with propulsion inhibited.
9. Test on the real trike with propulsion disabled, then wheels lifted: signs, limits, gear,
   PARK hold, HMI/manual takeover, ESTOP/reset, timeouts, and CAN failure.
10. Permit ground motion only after all required tests pass and evidence is recorded.

All three phases are required. Phase 2 is not optional.

---

## What stays the same (no change)

All existing CAN frame layouts, the safety layering, heartbeat messages, bus split,
steering/brake motor protocols, brake-pressure path, physical/HMI mode authority, and
emergency-stop behavior remain compatible. The additive `0x303` and `0x121` messages change
only the Host↔RT interface; SYS and MTR firmware remain unchanged. Existing RT PID, encoder,
resolver, diagnostics, bench, bypass, and unused/dormant code stays in place.
