# Three-Node Distributed Architecture

The E-Trike uses three physically separate compute nodes, each with a distinct role:

| Node | Hardware | OS / Framework | Role |
|------|----------|---------------|------|
| **Jetson** | Orin NX | Linux + ROS 2 | Perception, planning, high-level control |
| **RT** | ESP32-S3 @ 240 MHz | FreeRTOS (ESP-IDF) | Realtime physics, steering, CAN gateway |
| **SYS** | ESP32-S3 @ 240 MHz | FreeRTOS (ESP-IDF) | Safety, motor actuation, body control |

This is not an arbitrary split — each node runs a fundamentally different class of software.

---

## Why three nodes?

### 1. Different failure characteristics

| Node | Failure mode | Consequence |
|------|-------------|-------------|
| Jetson | Kernel panic, ROS node crash, OOM kill | Loses planning — vehicle must coast/stop safely |
| RT | Watchdog reset, CAN bus-off | Loses physics/steering — SYS heartbeats detect this |
| SYS | Watchdog reset, CAN bus-off | Loses motor/brake actuation. MTR maintains motor kill independently. Brake: SEB enters comm-fault on CAN loss (behavior unverified — hold or release). RT can still command EPS-C. Physical brake lever is a GPIO input to SYS — it cannot actuate SEB while SYS is rebooting (by-wire system, no mechanical hydraulic path). |

Each node has independent failure modes. A Jetson crash should not affect RT or SYS. An RT crash should not prevent SYS from keeping the vehicle stopped.

### 2. Different realtime guarantees

- **Jetson:** No hard realtime. Linux scheduler, memory pressure, disk I/O can introduce 10–100 ms jitter. Good enough for planning at 10–100 Hz.
- **RT ESP32:** Hard realtime. FreeRTOS with 1000 Hz tick, task priorities enforced by preemptive scheduler. Kinematics + PID at 100 Hz with <50 µs jitter. Steering CAN at 50 Hz with strict periodicity.
- **SYS ESP32:** Hard realtime. Safety GPIO poll at 20 Hz, motor DAC at 100 Hz, brake CAN at 50 Hz.

### 3. Different safety criticality

- **Jetson:** QM (quality managed). A bug here should be uncomfortable (jerky driving) but not dangerous. Safety envelope enforced by RT.
- **RT:** ASIL-like (safety critical). Must guarantee steering angle is safe, speed is limited, ESTOP propagates. Failure can cause injury.
- **SYS:** ASIL-like (safety critical). Must guarantee motor stops on ESTOP, brake engages, gear disengages. Failure can cause runaway.

### 4. Physical separation enables independent power domains

- Jetson runs on 12 V (from DC-DC).
- ESP32s run on 3.3 V (from 12 V via LDO).
- The 72 V traction domain is galvanically isolated from all compute.

---

## Responsibility split (complete)

| Concern | Jetson | RT | SYS |
|---------|:------:|:--:|:---:|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| CAN gateway (low ↔ high) | | ✓ | |
| Tricycle kinematics | | ✓ | |
| Speed PID | | ✓ | |
| Steering angle compute + CAN TX (`0x200`) | | ✓ | |
| Steering boot sync (LBS) | | ✓ | |
| Steering safety (dynamic clamp, hard-stops, following error) | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + button | | | ✓ |
| Brake lever → CAN (`0x720`, 50 Hz) | | | ✓ |
| Brake boot sync (LBS) | | | ✓ |
| Brake rolling counter + checksum | | | ✓ |
| DC-DC converter CAN control (`0x012`) | | | ✓ |
| Heartbeat monitoring | ✓ (RT, high) | ✓ (Jetson, high) | ✓ (RT, low) |
| Mode switch reading | | | ✓ |
| Throttle ADC read (0–5V) | | | ✓ |
| Throttle MCP4725 DAC output (0–5V) | | | ✓ |
| Gear 72V read (TLP281 opto) | | | ✓ |
| Gear 72V output (relay module) | | | ✓ |
| 12V accessory power relay | | | ✓ |
| Mode indicator lights | | | ✓ |
| Signal lights (turn, brake, head) | | | ✓ |
| System diagnostics | | | ✓ |

---

## Communication paths

```
Jetson ── High CAN ── RT ── Low CAN ── SYS ── Low CAN ── Actuators
  │                    │                   │
  │  0x300 drive cmd   │                   │
  │  0x301 brake req   │  0x202 drive sp   │
  │  0x302 lights      │  0x200 steer cmd  │
  │                    │                   │
  │  0x011 safety ◄────┤                   │
  │  0x120 throttle ◄──┤                   │
  │  0x210 state ◄─────┤                   │
  │  0x220 PID fb ◄────┤                   │
  │  0x400 obstacle ◄──┤                   │
  │  0x600 diag ◄──────┤                   │
  │                    │                   │  0x720 brake cmd → SEB
  │                    │                   │  0x012 dcdc cmd  → DC-DC
  │                    │                   │  0x721 SEB status ◄──
  │                    │  0x201 EPS-C ◄─────┤
```

**Key property:** Jetson never talks directly to SYS or to actuators. All commands pass through RT, which validates and clamps them.

---

## Dual-CAN on RT ESP32-S3

RT is the only node with two CAN interfaces. It uses two different controllers:

| Bus | Controller | Interface | Pins | Transceiver |
|-----|-----------|-----------|------|-------------|
| Low-level | Built-in TWAI | Direct GPIO | TX=5, RX=4 | SN65HVD230 |
| High-level | MCP2515 | SPI | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SN65HVD230 |

### Why two different controllers?

The ESP32-S3 has only one built-in TWAI controller (the S3 removed the second one that the original ESP32 had). To get two CAN buses on one MCU, we add an external MCP2515 via SPI.

- **TWAI (built-in):** Low-latency, hardware ACK, hardware filters, hardware TX mailbox. Used for the **low-level** bus (actuators) because steering commands need the lowest possible jitter (50 Hz fixed-rate).
- **MCP2515 (SPI):** Additional latency from SPI transaction (~10 µs per frame at 10 MHz SPI). Used for the **high-level** bus (Jetson) where 100 Hz telemetry jitter of 1–2 ms is acceptable.

### SPI bandwidth check

At 500 kbit/s CAN, worst-case frame is ~130 bits (extended ID, 8-byte DLC, stuff bits). That's ~260 µs on the bus. At 10 MHz SPI, reading a 13-byte MCP2515 RX buffer takes ~10 µs. SPI bandwidth is not a bottleneck.

---

## Why not a single ESP32?

An earlier design note (`rtos-architecture.md`) describes a single ESP32-S3 running all tasks on two cores with two TWAI controllers. That design has been superseded for these reasons:

1. **ESP32-S3 has only one TWAI.** The dual-TWAI design requires the original ESP32 (not S3), which has less SRAM and slower flash.
2. **No physical separation of safety.** If all software runs on one MCU, a single firmware bug (stack overflow, wild pointer, priority inversion) can take down both safety and actuation. With separate MCUs, SYS continues to function even if RT crashes.
3. **Independent power sequencing.** SYS powers up first and brings up the 12 V rail. RT powers up second. Jetson boots last (longest boot time). This ordering is harder to enforce with a single MCU.
4. **CAN bus isolation is physical, not just logical.** With two TWAI controllers on one chip, both buses share a silicon die — a latch-up on one CAN transceiver can propagate. Separate MCUs provide true galvanic isolation between bus domains (only connected through the CAN transceivers).

---

*See also: [[can-gateway-bridging]] for the forwarding rules between buses, [[defense-in-depth-safety]] for layered safety across nodes, [[achitecture]] §1 for topology, §5 for responsibility split, §7.2 for dual CAN hardware.*
