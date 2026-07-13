# E-Trike Operator Manual

> **Document Version:** 1.0.0-alpha
> **Vehicle:** E-Trike Drive-by-Wire Control System
> **Applicable Firmware:** v1.0.0-alpha

---

## 1. System Overview

The E-Trike is a three-wheeled electric vehicle with a drive-by-wire control system. There is no mechanical linkage between the rider controls and the actuators -- all commands are transmitted over Controller Area Network (CAN) buses to electronically controlled steering, braking, and motor systems.

The system consists of four primary electronic control units (ECUs):

- **Host Computer (Jetson Orin):** Perception, planning, and autonomous driving logic. Only active in AUTO mode.
- **RT Controller (ESP32-S3):** Real-time physics, steering control, CAN gateway between buses. Runs at 100 Hz for deterministic control.
- **SYS Controller (ESP32-S3):** Safety monitoring, body controls (lights, indicators, DC-DC converter), brake actuation, mode management.
- **MTR Controller (STM32):** Motor actuation via throttle DAC and gear relays. EGAS Level 1 function controller.

The vehicle has two operating modes — **MANUAL** and **AUTO** — selected via the mode button. **ESTOP** is a safety state triggered by the dedicated ESTOP button (or automatically by safety faults); it is not a mode and cannot be selected via the mode button.

---

## 2. Controls

### 2.1 Mode Button (MODE)

- **Location:** Handlebar, left side
- **GPIO:** SYS GPIO11
- **Short press (MANUAL or AUTO):** Toggles between MANUAL and AUTO mode
- **Short press (ESTOP):** Ignored
- **Long press (3s) (ESTOP):** Exits ESTOP to MANUAL (secondary exit path)

### 2.2 START Button

- **Location:** Handlebar, right side, green
- **GPIO:** SYS GPIO38
- **Action (ESTOP):** Exits ESTOP and enters MANUAL mode
- **Action (STEER_FAULT):** Short press resets steering state machine
- **Action (STEER_FAULT, long 3s + throttle zero):** Force-activates steering at 0 deg target (MANUAL only)

### 2.3 ESTOP Button

- **Location:** Handlebar center, large red mushroom button
- **Wiring:** Normally-closed (NC), active-low
- **GPIO:** SYS GPIO1, MTR kEstopGpio (dual-path, independent MCUs)
- **Action:** Immediately stops motor, engages full brake, opens gear relays, disables all non-safety loads

The ESTOP button is fail-safe by construction: a cut wire or disconnected plug reads as ESTOP.

### 2.4 Throttle Grip

- **Location:** Right handlebar grip
- **Type:** 0-5V analog signal
- **ADC:** SYS reads voltage, passes through to MTR via CAN in MANUAL mode
- **In AUTO mode:** Grip position is ignored (speed is Jetson-commanded)
- **In ESTOP:** Grip is ignored

### 2.5 Brake Lever

- **Location:** Left handlebar
- **GPIO:** SYS GPIO2 (digital input)
- **Action:** SYS reads lever state and transmits CAN 0x7B9 to brake-by-wire actuator (SEB)
- **In AUTO mode:** Lever always works and has priority over Jetson brake commands
- **In ESTOP:** Brake is already at maximum pressure

### 2.6 Turn Signal Switches

- **Location:** Handlebar, left/right toggle
- **GPIO:** SYS GPIO3 (left), GPIO6 (right)
- **Action:** Momentary press toggles blinker on/off for each side
- **Press both simultaneously:** Hazard flashers

### 2.7 Headlight Switch

- **Location:** Handlebar toggle
- **GPIO:** SYS GPIO7
- **Action:** Press to toggle headlight on/off

---

## 3. Display Indicators

The dashboard has two mode indicator bulbs and a brake light.

### 3.1 Mode Indicator Bulbs

| AUTO Bulb | MANUAL Bulb | Meaning |
|-----------|-------------|---------|
| OFF | ON | MANUAL mode -- rider is in full control |
| ON | OFF | AUTO mode -- Jetson is driving |
| OFF | OFF | ESTOP -- vehicle is emergency stopped |
| Blinking 2 Hz | OFF | Degraded steering: MANUAL only, AUTO locked out |

### 3.2 Brake Light

- **Location:** Rear of vehicle
- **Power:** Always-on DC-DC rail (independent of accessory relay)
- **Illuminates when:**
  - Brake lever is pressed (physical input)
  - ESTOP is active
  - Jetson CAN 0x302 commands brake light (supplemental only)

### 3.3 Turn Signals

- Left and right amber lamps
- 500 ms ON / 500 ms OFF blink pattern
- Hazard flashers when both switches active simultaneously

### 3.4 CAN Telemetry (Debug Tool)

When connected to the debug tool dashboard, the operator can view real-time telemetry including:

- Vehicle speed and gear state
- Steering angle (commanded and actual)
- Brake pressure and stroke
- Heartbeat status for all nodes
- Mode state and ESTOP reason codes
- Task health and alive counters

---

## 4. Operating Modes

### 4.1 MANUAL Mode

- **Control:** Rider controls throttle, gear, steering, brakes directly
- **Steering:** Handlebar is mechanically linked to EPS-C (standalone mode). RT does not transmit steering commands.
- **Throttle:** Grip position sampled by SYS ADC, sent to MTR via CAN
- **Brake:** Lever position sent by SYS to SEB via CAN 0x7B9
- **Gear:** Rider gear selector sent by SYS to MTR via CAN
- **Safety:** All safety layers remain active (following error, heartbeats, ESTOP)

Enter MANUAL by:
- Power-up (default mode)
- Pressing MODE button while in AUTO
- Exiting ESTOP via START button

### 4.2 AUTO Mode

- **Control:** Jetson computer handles perception, planning, and speed/steering commands
- **Steering:** RT receives 0x300 commands from Jetson, computes kinematics, transmits 0x169 to EPS-C
- **Throttle:** Speed setpoint from Jetson -> RT PID controller -> MTR DAC
- **Brake:** Jetson may request brake via 0x301/0x302; rider lever override always available
- **Safety:** All safety layers active. Rider can press ESTOP at any time. MODE button switches to MANUAL.

Enter AUTO by:
- Pressing MODE button while in MANUAL (vehicle stationary recommended)
- Jetson must be active and heartbeating

### 4.3 ESTOP Mode

- **This is the absorbing safety state.**
- **Triggered by:** ESTOP button press, CAN 0x001 broadcast, heartbeat timeout, steering following error, command staleness, or watchdog reset
- **What happens:**
  - Motor throttle: 0V (instant kill)
  - Gear: Neutral (all relays off)
  - Brake: Maximum stroke (~27 mm, full hydraulic pressure)
  - Steering: Non-obstacle -> ramps to 0 deg at 20 deg/s; Obstacle -> holds angle then silent-stops
  - DC-DC converter: Stays ON (MCUs need power)
  - Accessory relay: OFF (headlight, turn signals, mode bulbs off)
  - Brake light: ON (independent power)
- **Exit:** Only by pressing START button (-> MANUAL) or MODE long-press (3s, -> MANUAL). CAN commands cannot exit ESTOP. Power-cycle always exits.

---

## 5. Power-Up Sequence

1. **Insert key** and turn to ON position
2. **72V traction battery** engages -> DC-DC converter powers 12V rail
3. **MCUs boot** (MTR, SYS, RT in sequence):
   - SYS powers up first, brings up 12V accessory rail
   - RT powers up second
   - Jetson boots last (longest boot time)
4. **Listen Before Speaking (LBS):** Each actuator waits for the other side's status frame before transmitting commands:
   - Steering: RT waits for EPS-C 0x201 status, reads current angle, syncs to it
   - Brake: SYS waits for SEB 0x721 status, reads current stroke, syncs to it
5. **Heartbeat startup grace period:** 3 seconds mask heartbeat checks to prevent false ESTOP during boot
6. **Vehicle is ready** in MANUAL mode
7. **Jetson boots** and begins transmitting 0x7FC heartbeat and 0x300 drive commands
8. **Rider can switch to AUTO** via MODE button once Jetson is active

**Expected startup time:** ~5 seconds from key-on to MANUAL mode ready. Jetson boot adds another ~30 seconds.

---

## 6. Emergency Procedures

### 6.1 ESTOP Activation

**What the rider experiences:**
1. The vehicle decelerates firmly (full brake engages)
2. The steering either centers itself (non-obstacle) or holds position (obstacle)
3. The dashboard mode bulbs go dark (both OFF)
4. The brake light illuminates
5. The throttle grip stops responding
6. The gear selector stops responding

**What to do:**
1. Stay seated and hold the handlebars
2. Let the vehicle come to a complete stop
3. Assess the situation and check surroundings
4. Press the green START button to exit ESTOP into MANUAL mode
5. Verify throttle and steering respond before proceeding
6. If START button does not respond, power-cycle the vehicle (key off, then key on)

### 6.2 ESTOP Button Test (Pre-Ride)

1. Vehicle stationary, MANUAL mode
2. Press the red ESTOP button
3. Verify: Brake light ON, mode bulbs OFF, throttle grip has no effect
4. Press START button to recover
5. Verify: MANUAL bulb illuminates, throttle and steering respond

### 6.3 Jetson Failure During AUTO

If the Jetson computer fails during AUTO mode:
- The RT watchdog detects stale 0x300 drive commands within 500 ms
- Speed setpoint goes to zero, steering commands stop
- The vehicle coasts to a stop
- The rider can switch to MANUAL at any time via the MODE button
- The brake lever works normally throughout

### 6.4 Watchdog Reset

If the system resets unexpectedly:
1. The vehicle enters MANUAL mode automatically on reboot
2. Steering and brake actuators re-sync via the LBS sequence
3. Reset cause is logged internally
4. If resets recur, stop riding and contact support

### 6.5 Starting After ESTOP — Steering Deferred Ramps

When exiting ESTOP with steering in the process of centering (non-obstacle ESTOP):
1. Press START button
2. Brake immediately transitions from full to lever-controlled
3. Motor and gear transition to MANUAL immediately
4. Steering continues centering for up to 2 seconds (from full lock)
5. MANUAL bulb illuminates once steering handoff is complete
6. The vehicle is fully rideable in MANUAL

---

## 7. Pre-Ride Checklist

### Before Each Ride

- [ ] **Visual inspection:** Check tires, brakes, steering linkage for obvious damage
- [ ] **ESTOP test:** Press ESTOP button, verify dashboard goes dark and brake light illuminates. Press START to recover.
- [ ] **Brake test:** Squeeze brake lever, verify brake light illuminates, verify deceleration
- [ ] **Steering test:** Turn handlebars full left and right, verify smooth operation
- [ ] **Throttle test:** In MANUAL, twist throttle grip and verify motor response
- [ ] **Mode test:** Press MODE button, verify AUTO bulb illuminates (if Jetson is active), press again to return to MANUAL
- [ ] **Lights test:** Verify turn signals, headlight, and brake light operate correctly

### Before AUTO Mode Operation

- [ ] Confirm Jetson computer is booted and heartbeating (check debug tool)
- [ ] Verify all safety layers are green (heartbeats, no ESTOP)
- [ ] Ensure the path ahead is clear
- [ ] Keep hands near handlebars and feet near brake pedal
- [ ] Be prepared to press ESTOP at any time

### After Any Stop

- [ ] Verify mode bulbs indicate expected mode
- [ ] If both bulbs are OFF, the system is in ESTOP -- press START to recover
- [ ] If ESTOP recurs without obvious cause, do not ride -- investigate (contact support)

---

## 8. Safety Systems Reference

| System | What It Protects Against | Response Time |
|--------|-------------------------|---------------|
| Physical ESTOP button | Any hazard -- fastest path to stop | <1 ms (hardware), 20 Hz (software) |
| CAN 0x001 ESTOP | ESTOP propagation via CAN | Frame priority: highest |
| Heartbeat monitor | Node crash | 200-1500 ms depending on node |
| Command staleness | Host computer hang or software crash | 200-500 ms |
| Steering following error | Mechanical jam or actuator fault | 300 ms |
| Dynamic angle clamp | Rollover from excessive steering at speed | Every control cycle (100 Hz) |
| Software hard-stops | Steering mechanical damage | Every control cycle (100 Hz) |
| External watchdog | MCU firmware hang | 100 ms |

---

*Reference documents: [emergency-system](emergency-system.md), [defense-in-depth-safety](defense-in-depth-safety.md), [hardware-safety](hardware-safety.md), [listen-before-speaking](listen-before-speaking.md), [light-system](./light-system.md), [architecture-reference](./architecture-reference.md)*
