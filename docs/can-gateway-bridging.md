# CAN Gateway Bridging

In a multi-bus CAN architecture, the **gateway** is the single node connected to both buses. It selectively forwards messages between them, acting as a controlled bridge.

On the E-Trike, the **RT ESP32-S3** is the gateway between the High-Level CAN (Jetson + RT) and Low-Level CAN (RT + SYS + SYNTREE actuators).

---

## Why two buses?

Separating CAN buses serves three purposes:

1. **Security isolation.** Jetson runs a full Linux+ROS 2 stack — complex, harder to harden. The actuators (steering, brake) are on a physically separate bus. Even if Jetson is compromised, it cannot directly command steering or brakes. It must go through RT, which validates and clamps every command.

2. **Bus load partitioning.** High-level bus carries variable-rate ROS traffic (up to 100 Hz drive commands, telemetry). Low-level bus carries fixed 50 Hz actuator commands and safety status. Segregation prevents actuator commands from being delayed by bursty telemetry.

3. **Fault containment.** A bus-off on one bus doesn't take down the other. RT can continue operating actuators even if the Jetson link fails.

---

## Gateway topology

```
 High-Level CAN (500 kbit/s)          Low-Level CAN (500 kbit/s)
 ┌─────────────────────────┐          ┌─────────────────────────────┐
 │  Jetson                 │          │  SYS ESP32-S3               │
 │  (ROS 2, planning)      │          │  (safety, motor, brake)     │
 │                         │          │                             │
 │  TX: 0x300,0x301,0x302  │          │  TX: 0x011,0x012,0x110,    │
 │      0x001,0x7FD         │          │      0x120,0x600,0x7B9,    │
 │  RX: 0x011,0x120,0x210  │          │      0x001,0x7FD            │
 │      0x220,0x400,0x600  │          │  RX: 0x001,0x202,0x302,    │
 │      0x001,0x7FD         │          │      0x721,0x7FD            │
 └───────────┬─────────────┘          └──────────────┬──────────────┘
             │                                       │
             │          ┌──────────────┐             │
             └──────────┤ RT ESP32-S3  ├─────────────┘
                        │  (gateway)   │
                        │              │
                        │ TWAI +       │
                        │ MCP2515 SPI  │
                        └──────────────┘
```

RT is the **only** node on both buses. No direct Jetson ↔ SYS path exists.

---

## Forwarding rules

The gateway applies explicit, static forwarding rules. Not all messages cross the bridge.

### Low → High (upstream telemetry)

| ID | Name | Action |
|----|------|--------|
| `0x001` | SAFETY_ESTOP | **Forward transparently** (same ID, same payload) |
| `0x011` | SYS_SAFETY_STATUS | **Forward transparently** |
| `0x120` | SYS_THROTTLE_POS | **Forward transparently** |
| `0x600` | SYS_DIAG | **Forward transparently** |

### High → Low (downstream commands)

| ID | Name | Action |
|----|------|--------|
| `0x001` | SAFETY_ESTOP | **Forward transparently** (ESTOP from Jetson reaches SYS + actuators) |
| `0x302` | HOST_LIGHT_CMD | **Forward transparently** |

### Consumed locally (not forwarded)

| ID | Name | Why |
|----|------|-----|
| `0x300` | HOST_DRIVE_CMD | Consumed by RT — kinematics resolve it to `0x204` + `0x169` |
| `0x301` | HOST_BRAKE_REQUEST | Consumed by RT — brake arbitration (max-select) |

### Generated locally (not forwarded, originated by gateway)

| ID | Name | Bus | Why |
|----|------|-----|-----|
| `0x204` | RT_DRIVE_CMD | Low | RT output — speed+gear → MTR |
| `0x169` | VCU_SES_REQ | Low | RT output — steering angle → EPS-C |
| `0x210` | RT_STATE_REPORT | High | RT telemetry → Jetson |
| `0x220` | RT_PID_FEEDBACK | High | RT telemetry → Jetson |
| `0x400` | RT_OBSTACLE_DIST | High | RT telemetry → Jetson |

### Bus-local only (never cross the bridge)

| ID | Name | Bus | Why |
|----|------|-----|-----|
| `0x012` | SYS_DCDC_CMD | Low | DC-DC only on low bus |
| `0x110` | SYS_MODE_CMD | Low | Mode switch local to low bus |
| `0x201` | SES_STATUS | Low | EPS-C feedback — consumed by RT on low |
| `0x7B9` | VCU_SEB_REQ | Low | Brake command from SYS to SEB |
| `0x721` | SEB_STATUS | Low | Brake feedback to SYS |
| `0x7FD` | HEARTBEAT | Both | Independent per bus (each bus has its own heartbeat domain) |

---

## Transparent forwarding

"Transparent" means the gateway copies the frame verbatim: same CAN ID, same DLC, same payload bytes. The receiver cannot tell whether the frame originated from the original sender or passed through the gateway.

This is only safe because of the **one sender per ID** rule (except heartbeat). Since each CAN ID has exactly one originator on each bus, there's no ambiguity about which node sent it.

---

## Gateway implementation

On the RT ESP32-S3, the gateway runs in the `dispatch` task (priority 4):

```
dispatch_task:
  Block on can_rx_low_queue OR can_rx_high_queue (whichever fires first)

  Low bus frame received:
    0x011 → enqueue to gw_tx_high_queue    (forward SYS safety → Jetson)
    0x120 → enqueue to gw_tx_high_queue    (forward throttle pos → Jetson)
    0x600 → enqueue to gw_tx_high_queue    (forward diagnostics → Jetson)
    0x001 → enqueue to gw_tx_high_queue    (forward ESTOP → Jetson)
           + mode_set(Estop) locally
    0x201 → steer_feedback (sync angle, check following error)

  High bus frame received:
    0x300 → enqueue to cmd_queue            (drive command → control loop)
    0x301 → atomic store brake_request      (brake → arbitration)
    0x302 → enqueue to gw_tx_low_queue     (forward lights → SYS)
    0x001 → enqueue to gw_tx_low_queue     (forward ESTOP → SYS)
           + mode_set(Estop) locally
```

The `can_tx_low` and `can_tx_high` tasks drain the gateway TX queues and send the forwarded frames.

---

## Gateway queue full — drop policy

If a gateway TX queue is full, frames are dropped. The exception is `0x001` (SAFETY_ESTOP), which is sent directly (not queued) to guarantee delivery.

This is acceptable because:
- `0x011`, `0x120`, `0x600` are periodic — a dropped frame is replaced by the next one.
- `0x302` is change-triggered — if dropped, the next change retransmits.
- `0x001` is the only frame where one missed message could delay ESTOP, so it bypasses the queue.

---

## Key principles

1. **RT is the only dual-bus node.** No alternative path exists. All cross-bus traffic goes through RT.
2. **Transparent forwarding** keeps the same CAN ID — the receiver doesn't know or care about the gateway.
3. **Explicit allowlist, not a promiscuous bridge.** Only specific IDs cross. Everything else stays local to its bus.
4. **ESTOP bypasses queue.** The one frame that must never be dropped takes the fast path.
5. **Gateway is stateless for forwarding.** No buffering, no protocol translation, no sequence numbers — just copy and send.

---

*See also: [[can-addressing-for-etrike]] for the CAN ID scheme, [[distributed-architecture]] for three-node rationale, [[achitecture]] §2.3 for forwarding rules, §7.7 for gateway task layout.*
