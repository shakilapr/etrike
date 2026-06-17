# CAN Gateway Design

A **CAN gateway** is a node connected to two (or more) CAN buses that selectively forwards messages between them. It acts as a controlled bridge — not every frame crosses, and some are transformed in transit.

The E-Trike's RT ESP32-S3 is the gateway between the High-Level CAN (Jetson + RT) and Low-Level CAN (RT + SYS + actuators). It uses two different CAN controllers (built-in TWAI for low, external MCP2515 via SPI for high) and applies explicit forwarding rules.

---

## 1. Why Multiple CAN Buses?

A single CAN bus works for simple systems. Beyond ~50% bus load or when security isolation matters, you split into multiple buses.

### Reason 1: Bus load partitioning

Each CAN bus has finite bandwidth (500 kbit/s on the E-Trike). A standard frame with 8-byte payload takes ~130 bits, so max theoretical throughput is ~3,800 frames/s. At 50% target load: ~1,900 frames/s.

```
High bus load (if everything were on one bus):
  Jetson drive cmd:      100 Hz × 130 bits = 13,000 bps
  RT steer cmd:           50 Hz × 130 bits =  6,500 bps
  SYS throttle status:   100 Hz × 130 bits = 13,000 bps
  RT PID telemetry:       10 Hz × 130 bits =  1,300 bps
  EPS-C status:          100 Hz × 130 bits = 13,000 bps
  SEB status:            100 Hz × 130 bits = 13,000 bps
  ...
  Total: easily exceeds 50% load → low-priority messages delayed
```

Splitting into two buses keeps each bus under 30% load — plenty of headroom for bursts.

### Reason 2: Security isolation

Jetson runs a full Linux + ROS 2 stack — complex, network-connected, harder to harden. The actuators (steering, brake) are on a physically separate bus. Even if Jetson is fully compromised, it cannot directly command steering or brakes. Every command passes through RT, which validates and clamps.

```
Jetson (untrusted) ──► High CAN ──► RT (validates) ──► Low CAN ──► Actuators
```

### Reason 3: Fault containment

A bus-off on one bus doesn't take down the other. If the High CAN fails (e.g., Jetson CAN controller hangs), RT continues sending steering commands and heartbeats on the Low bus. SYS is unaffected and continues monitoring safety.

---

## 2. The Three Forwarding Categories

Every CAN message falls into exactly one category. This is an explicit design decision, not emergent behavior.

### Category 1: Transparent Forward

The gateway copies the frame to the other bus **unchanged** — same CAN ID, same payload, same DLC. The receiver cannot tell whether the original sender or the gateway transmitted it.

**When to use:** The message is relevant to nodes on both buses, and the format doesn't need translation.

```
SYS sends 0x011 on low bus ──► RT copies to high bus ──► Jetson receives 0x011
                             (same ID, same payload)
```

**Rule:** Transparent forward is safe only when each forwarded CAN ID has exactly **one sender per bus**. If SYS sends `0x011` and RT also generates `0x011`, forwarding creates ambiguity. The E-Trike enforces "one sender per ID" for all forwarded IDs.

### Category 2: Consume and Regenerate

The gateway receives a frame on one bus, processes it internally, and transmits a **different** CAN ID with **different** payload on the other bus. This is a translation, not a forward.

**When to use:** The message semantics change across buses, or the gateway adds validation/transformation.

```
Jetson sends 0x300 {speed, yaw} on high bus
    → RT consumes it
    → RT runs kinematics + PID
    → RT generates 0x202 {speed, gear} + 0x200 {angle} on low bus

The low-bus messages are completely different from the high-bus message that triggered them.
```

### Category 3: Bus-Local

The message never crosses the bus boundary. It serves only nodes on its own bus.

**When to use:** The message is only meaningful to nodes on one bus, or crossing would create a security/safety problem.

Examples from the E-Trike:
- `0x201` SES_STATUS (EPS-C → RT): Only RT needs steering feedback. Jetson gets processed telemetry via `0x210 RT_STATE_RPT`.
- `0x7FD` RT_HEARTBEAT (low bus): Heartbeats are per-bus liveness domains — bridging them creates the ambiguity described in [[heartbeat-monitoring]].
- `0x012` SYS_DCDC_CMD (SYS → DC-DC): The DC-DC converter is only on the low bus. Jetson has no reason to see or control it directly.

---

## 3. Gateway Implementation — The Dispatch Task

On the E-Trike, the gateway logic runs in RT's `dispatch` task (priority 4):

```
dispatch_task loop:
  1. Block on can_rx_low_queue OR can_rx_high_queue
  2. Dequeue frame
  3. Switch on CAN ID:
       Low bus IDs:
         0x001 → forward to high + mode_set(Estop) locally
         0x011 → forward to high
         0x120 → forward to high
         0x600 → forward to high
         0x201 → steer_feedback (consume locally, never forward)
         0x7FD → update RT heartbeat liveness (consume locally)

       High bus IDs:
         0x001 → forward to low + mode_set(Estop) locally
         0x300 → enqueue to cmd_queue (consume → generate 0x202 + 0x200)
         0x301 → atomic store brake_request (consume locally)
         0x302 → forward to low
         0x7FF → update Jetson heartbeat liveness (consume locally)

      All other IDs → ignore (bus-local)
  4. Enqueue forwarded frames to gw_tx_low_queue or gw_tx_high_queue
```

The actual CAN transmission happens in the `can_tx_low` and `can_tx_high` tasks — the dispatch task only queues, never blocks on TX.

---

## 4. ESTOP Bypasses the Queue

The gateway's TX queues have finite depth. If a queue is full, normal frames are dropped (the next periodic frame replaces them). But `0x001` (SAFETY_ESTOP) is the one frame that must never be dropped.

```
if (can_id == 0x001) {
    // Direct TX, bypass queue — guarantee delivery
    twai_transmit(&frame, 0);
    mcp2515_transmit(&frame, 0);
} else {
    // Normal path: enqueue for TX task
    xQueueSend(gw_tx_queue, &frame, 0);
}
```

This is a general pattern: the most critical safety signal takes the fast path. Everything else can tolerate queueing.

---

## 5. Gateway as Security Boundary

The gateway is a **policy enforcement point**. It doesn't just pass bits — it validates and clamps:

| Check | Where | What it prevents |
|-------|-------|-----------------|
| Steering angle clamp | RT control task, before `0x200` TX | Jetson commanding physically impossible or dangerous angles |
| Speed limit clamp | RT control task, before `0x202` TX | Jetson commanding overspeed |
| Obstacle speed limit | RT control task | Running into detected obstacles |
| Command staleness | RT watchdog task | Jetson hung → zero setpoints |

A transparent CAN-to-CAN bridge (like a dumb repeater) would forward anything. The gateway pattern gives you a point of control to enforce safety invariants.

---

## 6. Common Pitfalls

| Pitfall | What happens | Fix |
|---------|-------------|-----|
| **Promiscuous bridge** | Forwarding ALL frames between buses → bus load doubles, ID collisions, security bypass | Explicit allowlist for each direction |
| **Bridging heartbeats** | Two different alive counters, same CAN ID on one bus — ambiguous liveness | Never forward heartbeat frames. Each bus has its own. |
| **Queue depth too small** | Forwarded frames dropped under load | Profile peak bus load; size queues for 2× worst case |
| **Gateway task priority too low** | Frames pile up in RX queue while lower-prio tasks run | Dispatch task at priority 4 — just below CAN RX, above control |
| **Bridging with translation but keeping same ID** | Receivers see the same ID from two sources — which one is authoritative? | Each CAN ID has exactly one sender per bus. If translation is needed, use a different ID. |

---

*See also: [[can-protocol]] for arbitration and bus basics, [[heartbeat-monitoring]] for why heartbeats stay local, `architecture.md` §2.3 for the E-Trike's forwarding rules, §7.3–7.4 for gateway message tables.*
