# E-Trike → Autoware Universe — Plain Plan

## What this is

We are connecting the E-Trike to **Autoware Universe**, the current ROS 2 self-driving
software stack (we were previously aimed at the older "Autoware.Auto"). The goal is simple:
the trike should drive itself under Universe.

The good news: the trike's internal computers and its wired command bus mostly **stay
exactly as they are**. The only software that needs real work is the **bridge** — the program
on the Jetson computer that translates between Universe's messages and the trike's wired
commands.

---

## Plain glossary

| Term | Means |
|---|---|
| **Bridge** | The Jetson program that translates Universe messages ↔ trike wired commands. |
| **RT / SYS / MTR** | The trike's three small onboard computers: RT = motion control, SYS = safety & mode authority, MTR = motor driver. |
| **Wired bus (CAN)** | The cables carrying commands and sensor readings between the computers and the steering/brake motors. |
| **Message / topic** | How Universe sends a command, e.g. "steer left 5°", "go". |
| **Mode** | Who is driving: a human (**MANUAL**) or the computer (**AUTONOMOUS**). |
| **Engage** | The software switch that says "start driving now". Universe's canonical engage path is the AD-API topics `/api/autoware/get/engage` (state) and `/api/autoware/set/engage` (command). The older `/vehicle/engage` topic still exists but is legacy. |
| **ESTOP** | Emergency stop — cut motion immediately. |
| **PARK** | Hold the brake while stopped. |
| **Frame / signal** | One wired message / one value inside it. |

---

## The short version

1. **One component needs rewriting: the bridge.** Everything else can stay as-is for now.
2. Fix **6 specific things** in the bridge and the trike drives under Universe.
3. One later, **optional** firmware improvement lets it steer while parked — not required to drive.
4. Mode (who drives) and emergency stop are **safety decisions owned by SYS**, the trike's
   safety computer. The bridge only relays; it never decides these.

---

## What changes, what doesn't

| Component | Change? | In one line |
|---|---|---|
| **Bridge (Jetson)** | **YES — required** | Rewrite to Universe message types; fix direction, gear numbers, mode request, engage, emergency. This is the only work needed to drive. |
| **Wired commands (protocol)** | **NO (for now)** | Keep all existing messages exactly. New messages are optional later polish only. |
| **RT computer** | **NO (for now)** | Drives as-is. Only needed later for steering-while-parked. |
| **SYS computer** | **NO** | Already handles mode and emergency correctly; the bridge uses its existing path. |
| **control-toolkit** | **Phase 2 only** | Codecs auto-update from the shared protocol; the control-intent logic must also *send* the new `0x303` steer-angle frame (parallel to the bridge) so the bench tool can exercise standstill steering. |
| **simulation / debug-tool / vt-console** | **NO** | Regenerate only. |
| **Autoware settings** | **YES — small** | Lower speed/steer limits to trike size; keep mode timeout. |
| **New ROS packages** | **YES** | 4 small packages: bridge, protocol wrapper, launch, vehicle description. |

---

## The 6 things the bridge MUST fix

If any of these is wrong, the trike will not drive correctly. All six are bridge-only fixes.

1. **Build for Universe types.** The old bridge used message fields that no longer exist in
   Universe. Port it to the current `Control` message and remove the deleted `VehicleKinematicState`.
   (Without this it will not even compile.)

2. **Flip the steering direction.** The trike's internals measure steering as "right = positive",
   but Universe uses "left = positive". If unfixed, the trike steers the **opposite** way.
   Fix: negate the steering value when sending and when reading.

3. **Use the correct gear numbers.** Universe's "DRIVE" is the number **2**; the old code used
   **1** (which Universe reads as NEUTRAL). If unfixed, a "drive" command is ignored and the
   trike **never moves**. Fix: translate the numbers both ways from the message definitions.

4. **Answer the "go autonomous?" request and report autonomous.** Universe asks "switch to
   self-driving?" as a service and expects a yes/no. The bridge must answer, and then report
   the autonomous state using the trike's existing mode feedback. Without this, Universe never
   finishes engaging.

5. **Listen on the correct engage topic with correct delivery settings.** Universe's canonical
   engage path is the AD-API: `/api/autoware/get/engage` (state) and `/api/autoware/set/engage`
   (command), per `vehicle_cmd_gate.launch.xml:35,38`. The older `/vehicle/engage` topic still
   works in simulation but is legacy. The bridge should subscribe to the AD-API topic (or use
   a launch remap). Universe's commands also use a "keep last known value" delivery style, so
   the bridge must subscribe with matching settings.

6. **React to the emergency signal.** Universe sends an emergency flag (true/false). On true,
   the bridge must send the stop command. (See "Emergency stop" below for the important
   safety rule about clearing it.)

Close these six and the trike is **drivable under Universe end-to-end** on the existing wiring.

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
| Mode request (go autonomous) | send existing mode-request message `0x111` | SYS decides, broadcasts mode |

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

## Mode (who is driving) — a safety decision

**Mode is owned by SYS, the trike's safety computer. The bridge must never decide mode.**

- When Universe asks to switch to autonomous, the bridge sends the trike's **existing**
  mode-request message (`0x111`). The RT computer forwards it to SYS.
- SYS already arbitrates this message against the physical mode button and the emergency
  stop (and rejects it during an emergency). SYS is the sole authority that broadcasts the
  current mode.
- The bridge simply reports the mode SYS confirmed. It never invents a mode.

So no SYS firmware change is needed — the bridge reuses a path that already exists.

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
  field plus the held brake — functionally identical to park-hold. SYS/MTR execute the brake.

---

## The one firmware change that truly matters: steering while parked

**Why it is needed.** Today the trike's drive command carries only a *yaw rate* (how fast the
vehicle is turning), not a *steering angle* (which way the wheels point). Yaw rate is
meaningless at zero speed, so at a standstill the steering command is discarded — the wheels
stay centered and the trike cannot pre-position them. Universe, however, expects the wheel
angle to be tracked even when stopped (for pull-out, parking, and engaging from rest).

What currently happens at zero speed with only a yaw rate: **nothing** — the command is thrown
away by both the bridge and RT.

**Why it fails today.** The bridge converts Universe's steering angle to a yaw rate using
`yaw = speed × tan(angle) / wheelbase`. At zero speed, this yields yaw = 0 regardless of
angle — the precise angle is lost in the conversion. The bridge also explicitly zeroes yaw
below 0.05 m/s as a safety guard. So RT receives yaw ≈ 0, and its physics model decays
steering toward center.

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

This is the **only firmware change required** for a safe, standstill-capable system. All other
gaps are bridge-only or optional.

---

## Phase 2 — optional improvements (included in the plan)

These are **part of the plan**, done after Phase 1 proves the trike drives. They add fidelity
and safety reporting. They are **new messages only** — never edits to existing ones (old
firmware would reject a changed message). They are safe because they only add *feedback from*
the trike; the command path is untouched.

### 2.1 Steering angle at any speed — `0x303` (recommended; the one firmware MUST)

Lets the trike steer while parked (see "The one firmware change that truly matters").

- **Protocol:** add a new message `0x303` carrying the steering angle (signed, ±45°, right = positive).
- **RT firmware:** when `0x303` arrives, forward the angle to the steering motor and
  **bypass the low-speed steering cutoff**; keep the old yaw-rate path for other sources.
- **Bridge:** send `0x303` from Universe's steering angle; keep sending the existing drive
  command for speed.
- **Why safe:** new message ID; old firmware ignores it.

### 2.2 Motion report — `0x121` (fidelity)

Gives Universe an honest turn rate and the real gear state.

- **Protocol:** add a new message `0x121` carrying turn rate (computed by RT from speed ×
  angle) and the actual gear (including a "PARK rejected" state if the brake-hold failed).
- **RT firmware:** publish `0x121` at 10 Hz using speed and steering angle it already has.
- **Bridge:** use `0x121` for the speed report's turn-rate field and for an accurate gear
  report (including PARK feedback).

### 2.3 Mode detail — `0x211` (fidelity)

Lets the bridge report all 7 mode states and answer the engage request honestly.

- **Protocol:** add a new message `0x211` carrying the confirmed mode (all 7 states), the
  requested mode, and a reject reason.
- **RT firmware:** publish `0x211` from the mode SYS broadcasts.
- **Bridge:** report the richer mode states (e.g. "not ready", "disengaged") and include the
  reject reason in the engage answer.

### 2.4 Why these are safe to add

RT *receives* commands on the existing drive/brake/light messages. Adding `0x303/0x121/0x211`
only touches the **feedback** direction, so the command path is untouched. RT's receiver
ignores unknown message IDs, and the bridge is the only new consumer — old firmware will not
break.

### 2.5 Orphans kept

Not used by stock Universe, but kept for tooling/future use: obstacle distance, PID
telemetry, HMI mode request, headlight bits, brake/diagnostic reports. None are removed.

---

## The control_toolkit and the bridge — parallel paths, not chained

The **control_toolkit** (our bench engineering tool) talks **directly to the CAN bus** via a
USB adapter — it does NOT go through the bridge. It can replace the Jetson/Host role during
bench testing by sending the same CAN messages the bridge would (drive command, heartbeat,
mode request, ESTOP). In production, the bridge talks to Autoware + CAN; in bench testing,
the control_toolkit talks to CAN directly. They are parallel paths, not chained.

The control_toolkit already implements the same CAN protocol (same message codecs from the
shared `etrike_protocol` package), so its mode/ESTOP/gear handling is a reference for how the
bridge should work. The bridge just adds the Autoware ROS layer on top.

---

## Rollout

**Phase 1 — make it drive (bridge only):**
1. Rewrite the bridge and test against a virtual CAN bus (no firmware needed).
2. Add the 4 small ROS packages and the Autoware limit settings.
3. Validate end-to-end in simulation, then on the real trike (wheels lifted).

**Phase 2 — fidelity & standstill steering (optional, planned):**
4. Add the `0x303` angle message + RT firmware (steering while parked).
5. Add `0x121` motion report and `0x211` mode detail; bridge consumes them.
6. Regenerate codecs and re-validate.

Phase 1 alone is enough to drive. Phase 2 is included in the plan so the work is scoped and
ready, not an afterthought.

---

## What stays the same (no change)

The wired command format, the safety layering, the heartbeat messages, the split between the
two buses, the steering/brake motor protocols, the brake-pressure path, and the emergency-stop
behavior. The bridge is the only piece that must change to get moving.
