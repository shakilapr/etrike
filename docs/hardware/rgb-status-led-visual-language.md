# Onboard RGB Status LED Visual Language Specification

> **Applies To:** `sys-esp32` (Master Safety Authority) & `rt-esp32` (Motion Master & Gateway)  
> **Hardware:** Onboard Addressable WS2812 RGB LED (GPIO 48)  
> **Architecture:** Compositional Visual Grammar (`Domain Color × Temporal Modulation`)

---

## 1. Design Philosophy

Because a single RGB LED can only emit **one color at any instant**, multi-color indications (`Cyan + Red`, `Purple + Amber`, etc.) are achieved by **temporally replacing the base color with a crisp overlay pip**.

Instead of memorizing 25 arbitrary states, the operator learns **7 Domain Colors** and **6 Temporal Grammars**. Every state on both **SYS** and **RT** is a direct composition of these two tables, with identical visual semantics across both controllers.

---

## 2. The 7 Domain Colors (Where is the issue?)

Each color activates a distinct combination of physical LED dies, avoiding adjacent-hue ambiguity (e.g., Purple is used, Magenta is eliminated):

| Color | Calibrated RGB (Pre-Scale) | Active Dies | Domain / Subsystem |
| :--- | :--- | :--- | :--- |
| 🔴 **RED** | `(255, 0, 0)` | `R` | **Safety / ESTOP** |
| 🟠 **ORANGE** | `(255, 35, 0)` | `R + low G` | **Physical CAN / SPI Interface** |
| 🩵 **CYAN** | `(0, 180, 255)` | `G + B` | **Actuators** (MTR, SEB, SES) |
| 🟡 **YELLOW** | `(255, 130, 0)` | `R + G` | **Peer / Network Controllers** (Host, RT, SYS) |
| 🟪 **PURPLE** | `(140, 0, 255)` | `R + B` | **MANUAL Mode** (Rider control) |
| 🟢 **GREEN** | `(0, 255, 0)` | `G` | **AUTO Mode** (Autonomous control) |
| ⚪ **WHITE** | `(255, 255, 255)` | `R + G + B` | **Boot / System Initialization** |

> **Optical Calibration Note:** Master brightness is scaled to 25% peak (`max_val = 64`) to prevent workbench glare, and breathing uses a quadratic gamma curve ($I = x^2 / 256$) for smooth perceived luminance.

---

## 3. The Temporal Grammar (What is happening?)

### 3.1 Base Cadences

| Visual Element | Meaning | Timing Specification | Waveform |
| :--- | :--- | :--- | :--- |
| **SOLID** | Critical latched state or active motion | Continuous (`100%` duty) | `████████████████████` |
| **REMOTE ESTOP (`RED ↔ YELLOW`)** | Active remote ESTOP (follower) | `600 ms RED → 200 ms YELLOW` (`1.25 Hz` step) | `██████░░██████░░████` |
| **FAST BLINK** | Physical bus / interface failure | `150 ms ON → 150 ms OFF` (`3.3 Hz` hard toggle) | `██░░██░░██░░██░░██░░` |
| **BREATHING** | Stable idle / waiting / non-immediate | `1800 ms` cycle (`900 ms` up, `900 ms` down) | `▂▃▅▇█▇▅▃▂▃▅▇█▇▅▃▂▃▅▇` |

### 3.2 Temporal Overlay Pips

When an overlay is active, it **completely replaces** the base color for a brief pulse once per cycle (`1200 ms` total period = `200 ms` pip + `1000 ms` base color):

```text
Base Breathing with 200 ms Overlay Pip:

       ╭──── 1000 ms Base ────╮          ╭──── 1000 ms Base ────╮
BASE   ▂▃▅▇█▇▅▃▂              ░░░░░░░░░░ ▂▃▅▇█▇▅▃▂              ░░░░░░░░░░
                              ██████████                        ██████████
OVERLAY                       200 ms Pip                        200 ms Pip
```

| Overlay Color | Universal Semantic Meaning | Duration | Gap Between Pips |
| :--- | :--- | ---: | ---: |
| 🔴 **RED PIP** | **Fault / Rejected / Unsafe** (Internal unit error or control rejection) | `200 ms` | `1000 ms` |
| 🟠 **AMBER PIP** | **Modified / Blocked / Conflict** (Request not executed as asked) | `200 ms` | `1000 ms` |
| 🔵 **BLUE PIP** | **Upstream Source / Authority Missing** (Waiting on master/peer) | `200 ms` | `1000 ms` |
| ⚪ **WHITE TICK** | **Activity / Command Executing** (Live packet stream flowing) | `120 ms` | `880 ms` |

---

## 4. Compositional Semantics (Identical on SYS & RT)

Because every overlay has a fixed meaning, combinations are self-explanatory across both ECUs:

* **`CYAN` (Actuator) + `RED PIP` (Fault)** = Actuator reporting internal fault or rejecting control enable.
* **`CYAN` (Actuator) + `AMBER PIP` (Blocked)** = Actuator inhibit is blocking a requested drive command.
* **`PURPLE` (Manual) + `AMBER PIP` (Conflict)** = Vehicle is in Manual, but receiving autonomous drive commands.
* **`GREEN` (Auto) + `AMBER PIP` (Modified)** = Autonomous command is being clamped/limited by internal safety logic.
* **`YELLOW` (Peer) + `BLUE PIP` (Authority)** = Peer heartbeat missing while commands/authority are requested.
* **`WHITE` (Boot) + `BLUE PIP` (Authority)** = Peripherals booted, waiting for upstream safety authority.

---

## 5. Complete Priority Evaluation Order

Both controllers evaluate their state from **Priority 0 (highest)** to **Priority 6 (lowest)**. The first matching condition determines the LED output.

### 5.1 SYS (`sys-esp32`) Priority Cascade

| Priority | Base Color | Cadence / Overlay | Trigger Condition | Diagnostic Meaning |
| :---: | :--- | :--- | :--- | :--- |
| **P0** | ⚫ **OFF** | Continuous | Unpowered / panic | No 3.3V power or CPU halted. |
| **P1** | 🟠 **ORANGE** | **Fast Blink** (`150/150 ms`) | TWAI Bus-Off | Physical Low CAN bus shorted, open, or un-terminated. |
| **P2.0** | 🔴 **RED** | **Solid** | Local ESTOP tripped | Hardware mushroom button open (GPIO 1), EGAS mismatch ($>500\,\text{mm/s}$), or task stall. |
| **P2.1** | 🔴 **RED** | **Solid + White Tick** (`120 ms`) | Local ESTOP + MTR ACK retry | ESTOP active; SYS is actively retrying `0x206` ACK check. |
| **P2.2** | 🔴 **RED** | **`600 ms RED / 200 ms YELLOW`** | Remote ESTOP active | Received `0x001` from RT or Host; local hardware inputs intact. |
| **P3.0** | 🩵 **CYAN** | **Breathe + Red Pip** (`200 ms`) | Latched actuator fault | SEB Level 3 error (`kLatchedSebL3`), persistent brake following error, or MTR fault flags. |
| **P3.1** | 🩵 **CYAN** | **Breathe + Amber Pip** (`200 ms`) | Inhibit + drive requested | `kInhibitMtrFbkLoss` / `kInhibitSebCommsLoss` active while speed command $>0$ is blocked. |
| **P3.2** | 🩵 **CYAN** | **Breathe** (`1800 ms`) | Transient inhibit (idle) | MTR `0x206` or SEB `0x721` missing at standstill. |
| **P4.0** | 🟡 **YELLOW** | **Breathe + Blue Pip** (`200 ms`) | RT missing + drive requested | RT `0x7FD` timed out while command signals are attempting motion. |
| **P4.1** | 🟡 **YELLOW** | **Breathe + Amber Pip** (`200 ms`) | Stale RT `0x204` command | RT heartbeat alive, but `0x204` drive command stream froze/stale. |
| **P4.2** | 🟡 **YELLOW** | **Breathe** (`1800 ms`) | RT heartbeat missing (idle) | RT `0x7FD` timed out at standstill. |
| **P5.0** | ⚪ **WHITE** | **Breathe** (`1800 ms`) | Boot grace (`< 3000 ms`) | Cold start, no CAN peers seen yet. |
| **P6.0** | 🟪 **PURPLE** | **Breathe + Amber Pip** (`200 ms`) | Manual + Auto cmd conflict | Mode is MANUAL, but RT/Host is sending non-zero `0x204` commands. |
| **P6.1** | 🟪 **PURPLE** | **Solid + White Tick** (`120 ms`) | Manual driving / braking | Rider twisting throttle or pulling brake lever (GPIO 2). |
| **P6.2** | 🟪 **PURPLE** | **Breathe** (`1800 ms`) | Manual standstill | Rider in control, 72V armed, vehicle at rest. |
| **P6.3** | 🟢 **GREEN** | **Solid + Amber Pip** (`200 ms`) | Auto + brake lever override | Mode is AUTO, but rider brake lever is overriding RT brake request. |
| **P6.4** | 🟢 **GREEN** | **Solid + White Tick** (`120 ms`) | Auto actively driving | Mode is AUTO, fresh `0x204` speed $>0$ executing. |
| **P6.5** | 🟢 **GREEN** | **Breathe** (`1800 ms`) | Auto standstill | Mode is AUTO, all systems healthy, speed $= 0$. |

---

### 5.2 RT (`rt-esp32`) Priority Cascade

| Priority | Base Color | Cadence / Overlay | Trigger Condition | Diagnostic Meaning |
| :---: | :--- | :--- | :--- | :--- |
| **P0** | ⚫ **OFF** | Continuous | Unpowered / panic | No 3.3V power or CPU halted. |
| **P1** | 🟠 **ORANGE** | **Fast Blink** (`150/150 ms`) | TWAI Bus-Off or MCP2515 SPI fail | Low CAN bus-off, High CAN bus-off, SPI parity error, or TX buffer exhaustion. |
| **P2.0** | 🔴 **RED** | **Solid** | Local RT ESTOP tripped | Steering following error ($>2^\circ$), MTR timeout in AUTO, stale `0x300`, or obstacle halt. |
| **P2.1** | 🔴 **RED** | **Solid + Blue Pip** (`200 ms`) | Emergency SEB brake takeover | SYS `0x7B9` vanished; RT took over emergency max-brake transmission (`EMERGENCY_FALLBACK`). |
| **P2.2** | 🔴 **RED** | **`600 ms RED / 200 ms YELLOW`** | Remote ESTOP active | Received `0x001` or SYS `0x011`/`0x110` reports ESTOP. |
| **P3.0** | 🩵 **CYAN** | **Breathe + Red Pip** (`200 ms`) | Actuator fault / rejection | SES control enable rejected or SES `0x201` reporting internal error status. |
| **P3.1** | 🩵 **CYAN** | **Breathe + Amber Pip** (`200 ms`) | Actuator missing + cmd active | SES `0x201` or MTR `0x206` missing while Host is commanding motion. |
| **P3.2** | 🩵 **CYAN** | **Breathe** (`1800 ms`) | Actuator missing (idle) | SES `0x201` or MTR `0x206` silent at standstill. |
| **P4.0** | 🟡 **YELLOW** | **Breathe + Blue Pip** (`200 ms`) | Peer missing + cmd active | SYS `0x7FE` or Host `0x7FC` missing while drive command is requested. |
| **P4.1** | 🟡 **YELLOW** | **Breathe** (`1800 ms`) | Host or SYS heartbeat missing | Jetson `0x7FC` disconnected (standby) or SYS `0x7FE` lost. |
| **P5.0** | ⚪ **WHITE** | **Breathe + Blue Pip** (`200 ms`) | `g_no_sys_authority == true` | Booted, waiting for SYS `0x011`/`0x110` authority stream. |
| **P5.1** | ⚪ **WHITE** | **Breathe** (`1800 ms`) | Cold start (`< 3000 ms`) | Just powered, no bus peers connected yet. |
| **P6.0** | 🟪 **PURPLE** | **Breathe + Amber Pip** (`200 ms`) | Manual + Host cmd conflict | Mode is MANUAL, but Host is sending non-zero `0x300` drive commands. |
| **P6.1** | 🟪 **PURPLE** | **Breathe** (`1800 ms`) | Manual mode passthrough | Mode is MANUAL, RT actuator outputs silent. |
| **P6.2** | 🟢 **GREEN** | **Breathe + Amber Pip** (`200 ms`) | Zero-speed spin-in-place lockout | Mode is AUTO at standstill ($v=0$), Host requested yaw $\omega \ne 0$ (locked out). |
| **P6.3** | 🟢 **GREEN** | **Solid + Amber Pip** (`200 ms`) | Auto driving + safety clamp | Mode is AUTO driving, dynamic angle clamp or slew limiter actively modifying command. |
| **P6.4** | 🟢 **GREEN** | **Solid + White Tick** (`120 ms`) | Auto actively driving | Mode is AUTO, fresh `0x300` executing nominal kinematics. |
| **P6.5** | 🟢 **GREEN** | **Breathe** (`1800 ms`) | Auto standstill | Mode is AUTO, all peers healthy, speed $= 0$. |

---

## 6. Pin Assignment Prerequisite

On `esp32-s3-devkitc-1`, the onboard WS2812 RGB LED is hardwired to **GPIO 48**.
* **RT (`rt-esp32`)**: GPIO 48 is free.
* **SYS (`sys-esp32`)**: `kBulbAuto` must be moved from GPIO 48 to a free pin (e.g. **GPIO 10**) so GPIO 48 is dedicated to the WS2812 RMT channel.
