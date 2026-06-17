# CAN Hardware — Physical Implementation Basics

When moving from theory to physical implementation, CAN is unforgiving if hardware fundamentals are ignored. These are the non-negotiable basics — the things that cause 90% of failures in embedded systems.

---

## 1. The 120Ω Termination Rule

CAN relies on voltage differentials, making it susceptible to signal reflections bouncing from the ends of the wires.

- **Exactly two** 120Ω resistors — one at each extreme physical end of the main bus line, across CAN_H and CAN_L.
- Do **not** put terminators on branch nodes (stubs).
- Do **not** use more or fewer than two terminators.
- With power off, measuring across CAN_H to CAN_L should read **~60Ω** (two 120Ω in parallel).
- Tolerance: 120Ω ±5% or ±10% is fine; 118Ω works (under 2% difference).

**Symptom of missing/wrong termination:** Intermittent comms, corrupted frames, or total failure — especially worse at higher bitrates.

```
Correct termination layout:

  120Ω ---+-----+-----+-----+--- 120Ω
          |     |     |     |
        Node  Node  Node  Node
        (short stub, no termination)
```

---

## 2. The "Two-Wire" Myth — Common Ground

CAN is often advertised as a "two-wire" protocol. **This is misleading.**

- While signaling is differential, the transceivers need a common reference frame to keep voltages within their common-mode range.
- If nodes are powered by separate, isolated power supplies, you **must** run a third wire connecting their grounds (CAN_GND).
- Failing to tie grounds together is a primary cause of transceivers burning out or dropping packets in industrial environments.
- **Alternative:** Use galvanic isolation for the CAN bus.

---

## 3. Controller vs. Transceiver

Microcontrollers (ESP32, STM32, etc.) often advertise a "CAN Interface" — but this means only a CAN **Controller** is built into the silicon.

| Component | What it does | Built into MCU? |
|-----------|-------------|-----------------|
| **CAN Controller** | Handles logic, framing, arbitration, CRC | Sometimes (e.g., ESP32 TWAI, STM32 bxCAN/FDCAN) |
| **CAN Transceiver** | Converts TX/RX logic levels to differential CAN_H/CAN_L voltages | **Never** — always a separate IC |

You **always** need an external CAN transceiver IC (e.g., TJA1050, SN65HVD230, MCP2551) between the microcontroller's TX/RX pins and the physical bus wires.

**A known-good beginner bench setup:**

```
MCU/SoC A                  Twisted pair bus                     MCU/SoC B
+----------+        +---------------------------+        +----------+
| CAN ctrl |  TXD   |                           |   TXD | CAN ctrl |
|         o--------| Transceiver A      B      |-------o         |
|         o--------| RXD             RXD       |-------o         |
+----------+        |     CANH ======== CANH    |        +----------+
                    |     CANL ======== CANL    |
                    |       |               |    |
                    |      120Ω           120Ω  |
                    |       |               |    |
                    |      GND-----------GND    |
                    +---------------------------+
```

The important beginner rule: **termination belongs at the two physical ends of the main cable**, not at every node. CAN_H, CAN_L, and a ground reference must all be connected: CAN_H→CAN_H, CAN_L→CAN_L, GND→GND, firm wiring, and matching bitrate across all nodes.

```
MCU (ESP32/STM32)                    Bus
+------------------+       +--------+==== CAN_H
| CAN Controller   |--TXD--|        |
|                  |--RXD--|  CAN   |
+------------------+       | Trans- |
                           | ceiver |
                           |        |==== CAN_L
                           +--------+
```

**Symptom of missing transceiver:** TXD toggles but CAN_H/CAN_L stay flat — no frames seen externally.

> Also verify the transceiver is in **normal mode** (not standby/silent). On Microchip MCP2561/2, a low STBY pin selects normal mode; floating it can put the part into standby. Check your transceiver's datasheet.

---

## 4. Wire Twisting is Not Optional

- CAN_H and CAN_L **must** be tightly twisted together (twisted pair).
- Twisting ensures external electromagnetic interference (EMI) strikes both wires equally. Because the receiver only looks at the *difference* between the two voltages, the common-mode interference is mathematically canceled out.
- Loose jumper wires might work on a clean lab bench, but on a mobile chassis near high-current motor drivers or actuators, they will fail.

---

## 5. Bus Topology

**Wrong for bring-up at speed (star topology):**

```
          Node A
            |
Node B ---- Star ---- Node C
            |
          Node D
```

This feels neat on a bench, but T-connections and star-like layouts increase reflections. The problem gets worse as bitrate rises.

**Correct: Linear trunk with short stubs.**

```
120Ω ---+-----+-----+-----+--- 120Ω
        |     |     |     |
      Node  Node  Node  Node
      <30cm  <30cm  <30cm  <30cm
       stub   stub   stub   stub
```

**Wrong topologies to avoid:**

```
❌ Star topology — reflections at the junction kill signal integrity
❌ Long stubs (>30cm at 1 Mbps) — impedance discontinuity causes reflections
❌ Ring topology — not supported by CAN
```

### Bus Length vs. Bitrate

| Bitrate | Max Bus Length | Max Stub Length |
|---------|---------------|-----------------|
| 1 Mbit/s | 40 m | 0.3 m |
| 500 kbit/s | 100 m | 0.5 m |
| 250 kbit/s | 200 m | 1 m |
| 125 kbit/s | 500 m | 2 m |
| 50 kbit/s | 1000 m | 5 m |

Higher bitrates demand shorter buses and shorter stubs.

---

## 6. Managing Bus Load

Because CAN is broadcast-based, dumping data indiscriminately will cripple the network.

- **Never** put CAN transmissions in an unconstrained loop.
- Keep average bus load **below 50%**. Above 70%, lower-priority messages are systematically starved of bus access, leading to timeouts and control lag.
- Optimize by reducing message frequency, payload size, or segmenting the network.
- A CAN analyzer can display real-time bus load.

---

## 7. Minimum Two Nodes

CAN requires at least **2 active nodes** on the bus. A frame's ACK slot must be driven dominant by a receiver — without an ACK, the transmitter enters Error Passive mode, then Bus Off.

- Testing with a single board and a passive analyzer won't work (analyzers in listen-only mode don't ACK).
- For solo development: use internal loopback mode, or connect a second active node.

---

*See also: [[can-protocol]] for protocol theory, [[can-troubleshooting]] for debugging procedures, [[can-addressing-for-etrike]] for our project's ID assignments.*
