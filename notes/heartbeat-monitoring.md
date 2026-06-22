# Heartbeat & Liveness Monitoring

In a distributed system, a node can fail silently. Its CAN controller might keep retransmitting the last frame from a DMA buffer while the CPU is frozen. Its power supply might dip below brownout threshold. Its crystal might drift out of tolerance. **Liveness monitoring** answers the question: *is the node on the other end of the bus still alive and thinking?*

The E-Trike uses heartbeat frames (`0x7FD`, `0x7FE`, `0x7FC`) on every CAN bus for this purpose.

---

## 1. The Problem: Silent Failure

Consider a CAN bus without heartbeats. Node A sends commands at 50 Hz. Node B receives them. One day, Node B's CPU freezes but its CAN controller (a separate hardware block) keeps DMA-ing the last valid frame from its TX mailbox. Node A sees a perfect 50 Hz stream and assumes Node B is healthy.

Node B is dead. Node A doesn't know. The system continues operating with no safety monitor.

**This is not theoretical.** It happens when:
- A watchdog reset leaves the CAN controller initialized but the application hung in a boot loop.
- A power glitch browns out the CPU core but not the CAN transceiver.
- A stack overflow corrupts the task but the CAN TX mailbox was already loaded.

---

## 2. The Alive Counter — Why Not Just "Frame Present"?

A naive heartbeat sends an empty frame periodically. The receiver checks "did I get a frame recently?" But with the DMA replay problem above, the CAN controller answers "yes" — forever.

The fix is an **alive counter**: a single byte that increments every frame.

```
Frame 1: alive_ctr = 0x01
Frame 2: alive_ctr = 0x02
Frame 3: alive_ctr = 0x03   ← CPU freezes here
Frame 4: alive_ctr = 0x03   ← CAN controller replays frame 3 forever
Frame 5: alive_ctr = 0x03   ← receiver detects: same counter twice → node is dead
```

The receiver stores the last counter value. If a new frame arrives with the same counter, the node is frozen — even if the frame timing is perfect.

```cpp
bool heartbeat_is_fresh(uint8_t new_ctr) {
    if (new_ctr != last_ctr) {
        last_ctr = new_ctr;
        return true;       // counter changed — node is alive
    }
    return false;          // frozen — same counter twice, CAN controller on autopilot
}
```

---

## 3. Timeout Selection — FTTI

The **Fault Tolerant Time Interval (FTTI)** is the maximum time a fault can persist before it becomes hazardous. It comes from ISO 26262 (functional safety) and drives every timeout in a safety-critical system.

| Bus | Timeout | Rationale |
|-----|---------|-----------|
| Low CAN (RT↔SYS) | **200 ms** | At 25 km/h (~7 m/s), the trike travels ~1.4 m. A steering fault persisting longer than this puts the vehicle in an adjacent lane or off the road. |
| High CAN (RT↔Jetson) | **500 ms** | Jetson failure results in a *controlled stop* (zero setpoints), not an immediate ESTOP. The rider can still override. 500 ms is long enough to survive a Jetson hiccup, short enough that the vehicle doesn't coast into an intersection. |

**The rule:** faster vehicle × more critical system = shorter timeout. 200 ms for inter-MCU is an automotive-grade choice. A passenger car at highway speed demands <100 ms FTTI for steering; at 25 km/h, 200 ms is conservative.

---

## 4. Startup Grace Period

At boot, no heartbeats have been exchanged yet. If you check immediately, you'd trigger ESTOP before any node has had a chance to send its first frame.

**Solution:** a startup grace period (typically 3 seconds) where `heartbeat_ok()` returns `true` regardless.

```cpp
bool heartbeat_ok() {
    int64_t now = esp_timer_get_time();
    if (last_hb_us == 0) {
        // Never received a heartbeat yet — still in startup
        return (now < kStartupGraceUs);  // 3 seconds
    }
    return (now - last_hb_us) < kTimeoutUs;  // 200 ms
}
```

After the grace period, the safety monitor arms and real heartbeat checking begins. If a node never comes online, the grace period expires and ESTOP triggers — preventing operation with a dead safety node.

---

## 5. Independent Buses, Independent Heartbeats

Heartbeat frames MUST NOT be bridged between CAN buses. Each bus is an independent liveness domain.

**Why bridging heartbeats creates ambiguity:**

```
Low bus:  SYS sends 0x7FE {ctr=42}, RT sends 0x7FD {ctr=17}
          ↓ RT bridges SYS's 0x7FE to high bus
High bus: Jetson receives 0x7FE {ctr=42} — whose counter is this?
          SYS? RT? Is the counter monotonic? Which node died?
```

With the same CAN ID on the same bus, you can't tell which sender is which. The E-Trike now uses separate IDs for separate nodes on the low bus (`0x7FD` for RT, `0x7FE` for SYS) and monitors each independently.

**The liveness matrix** (who monitors whom):

| Monitor | Watches | Bus | Timeout | Action on loss |
|---------|---------|-----|---------|---------------|
| SYS | RT (`0x7FD`) | Low | 200 ms | ESTOP (AUTO only) |
| RT | SYS (`0x7FE`) | Low | 200 ms | CAN `0x001` ESTOP |
| RT | Jetson (`0x7FC`) | High | 500 ms | Zero setpoints |
| Jetson | RT (`0x7FD`) | High | 500 ms | Stop publishing |

RT monitors both buses because it's the gateway — it's the only node that knows about failures on both sides.

---

## 6. Heartbeat vs. External Watchdog

A heartbeat catches a frozen node on the CAN bus. An **external watchdog** catches a frozen MCU regardless of CAN state (see [[external-watchdog]]). They're complementary:

| Failure | Heartbeat catches? | External watchdog catches? |
|---------|:------------------:|:--------------------------:|
| CPU hung, CAN controller alive | ✓ (same counter twice) | ✗ (watchdog GPIO may still toggle from DMA) |
| CPU hung, CAN controller dead | ✓ (no frames) | ✓ |
| Crystal failure | ✓ (no frames) | ✓ (watchdog IC has independent oscillator) |
| CAN transceiver dead | ✓ (no frames from that node) | ✗ (MCU is fine) |
| Power brownout | ✓ (no frames) | ✓ |

Neither alone is sufficient for safety-critical systems. Both together cover the failure space.

---

## 7. Common Pitfalls

| Pitfall | What happens | Fix |
|---------|-------------|-----|
| **Empty heartbeat frame** | CAN controller replays it forever — receiver can't detect CPU freeze | Always include an alive counter byte |
| **Heartbeat on same ID from multiple nodes** | Ambiguous which node is dead | Assign a unique CAN ID to each heartbeat sender, or use separate buses |
| **Bridging heartbeats** | Two different counters on the same bus with the same ID — receiver can't distinguish | Never forward heartbeat frames. Each bus is its own liveness domain. |
| **Timeout too short** | Transient bus errors cause false ESTOP | Set timeout ≥ 3× the heartbeat period. At 2 Hz (500 ms), timeout ≥ 150 ms. |
| **Timeout too long** | Vehicle travels dangerous distance before ESTOP | Bound by FTTI. At 25 km/h with 200 ms timeout: 1.4 m travel. |
| **No startup grace period** | ESTOP at boot before any heartbeats are sent | Mask liveness checks for the first 3 seconds |

---

*See also: [[external-watchdog]] for hardware watchdog, [[can-gateway-bridging]] for why heartbeats stay local, `architecture.md` §8.6 for heartbeat implementation, `can-dictionary.md` §1 0x7FD/0x7FE/0x7FC for frame layout.*
