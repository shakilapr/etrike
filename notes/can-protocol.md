# How CAN Works — Protocol & Theory

**Controller Area Network (CAN)** is a robust, multi-master, message-based serial communication protocol. Originally developed by Bosch for automotive multiplex wiring, it is now a foundational standard in robotics, industrial automation, and embedded systems.

---

## Physical Layer

CAN uses a differential two-wire bus: **CAN High (CAN_H)** and **CAN Low (CAN_L)**. Nodes do not have a master-slave relationship; any node can broadcast when the bus is idle.

> ⚠️ **The "two-wire" label is a trap.** While signaling is differential, the transceivers need a common voltage reference. If your nodes run on separate, isolated power supplies, you **must** run a third wire connecting their grounds (CAN_GND). Without it, voltages drift beyond the common-mode range — transceivers burn out or drop packets. See [[can-hardware-basics#2-the-two-wire-myth-common-ground]].

### Logic States

Instead of standard high/low voltages, CAN uses two states based on the voltage *difference* between CAN_H and CAN_L:

| State | Meaning | CAN_H | CAN_L | Differential |
|-------|---------|-------|-------|-------------|
| **Dominant** | Logic 0 | ~3.5V | ~1.5V | ~2V |
| **Recessive** | Logic 1 | ~2.5V | ~2.5V | ~0V (idle) |

**The Golden Rule:** A Dominant bit (0) physically overwrites a Recessive bit (1) on the bus. This is the physical mechanism that makes collision resolution possible.

---

## Addressing & Priority

CAN is **content-centric**, not node-centric. A standard CAN frame does not contain sender or receiver addresses. Instead, it broadcasts a **Message Identifier (ID)**.

- **Message Identifier:** The ID says *what* the data is (e.g., motor RPM, steering angle, sensor fault). Every node receives every message, checks the ID against its hardware acceptance filters, and decides whether to process or ignore it.
- **Priority:** The ID also determines message priority. **Lower ID = higher priority.** A message with ID `0x001` will always beat `0x050` in arbitration.

---

## Bitwise Arbitration (CSMA/CD+AMP)

Because any node can transmit, collisions are inevitable. CAN handles them without data loss using **Carrier Sense Multiple Access / Collision Detection + Arbitration on Message Priority**.

When two or more nodes begin transmitting simultaneously, they undergo **Non-Destructive Bitwise Arbitration**:

1. Both nodes transmit their Identifier bits starting with the most significant bit (MSB), while simultaneously monitoring the bus.
2. If Node A transmits a Recessive bit (1) but Node B transmits a Dominant bit (0), Node B's dominant bit pulls the bus to the Dominant state.
3. Node A reads the bus, sees a 0 despite transmitting a 1, and **immediately realizes it has lost arbitration**.
4. Node A instantly stops transmitting and switches to receiving mode. Node B continues its payload **uninterrupted** — no data is lost.

> **Key insight:** The lower the ID number, the more leading zeros (dominant bits), so lower IDs always win arbitration. This is why safety-critical messages get the lowest IDs.

---

## Frame Types

There are four primary frame types:

### 1. Data Frame
The standard frame for transmitting payload data from a transmitter to receivers.

| Field | Purpose |
|-------|---------|
| SOF | Start of Frame (single dominant bit) |
| ID | 11-bit or 29-bit identifier; lower = higher priority |
| RTR | Remote Transmission Request (dominant for data frames) |
| Control | IDE bit, reserved bit, DLC (Data Length Code: 0–8 bytes) |
| Data | Payload (0–8 bytes Classical, up to 64 bytes FD, up to 2048 bytes XL) |
| CRC | Cyclic Redundancy Check for error detection |
| ACK | Receiver acknowledges by overwriting the ACK slot with a dominant bit |
| EOF | End of Frame (7 recessive bits) |

### 2. Remote Frame
A node requests specific data by sending a frame with the RTR bit set to recessive. The node holding that data responds with a Data Frame. *Deprecated in CAN FD and CAN XL.*

### 3. Error Frame
If any node detects a protocol or CRC error, it immediately broadcasts an Error Frame (a deliberate sequence of 6 dominant bits). This violates the bit-stuffing rules, causing all other nodes to discard the corrupted message and forcing retransmission.

### 4. Overload Frame
Used by a receiving node to request a delay between consecutive frames if overwhelmed. Rarely used in modern fast microcontrollers.

---

## CAN Standards Evolution

### Classical CAN (CAN 2.0, ISO 11898-1:2015)

| Variant | ID Length | Unique IDs | Payload | Max Baud |
|---------|-----------|------------|---------|----------|
| CAN 2.0A (Standard) | 11-bit | 2,048 | 8 bytes | 1 Mbit/s |
| CAN 2.0B (Extended) | 29-bit | 536+ million | 8 bytes | 1 Mbit/s |

### CAN FD (Flexible Data-Rate)

Introduced to solve the bandwidth bottleneck of Classical CAN.

- **Dual bitrates:** Standard slower speed (e.g., 500 kbit/s) during arbitration, switching to a faster speed (2–8 Mbit/s) during the data payload phase.
- **Payload:** Up to **64 bytes**.
- **Compatibility:** Fully backward compatible (an FD node understands Classical CAN; a Classical node will error on FD frames).

### CAN XL (ISO 11898-1:2024)

The third generation, bridging CAN FD and 100BASE-T1 Automotive Ethernet.

- **Data phase:** Up to **20 Mbit/s** (using push-pull signaling via CAN SIC XL transceivers).
- **Payload:** Up to **2048 bytes** — can encapsulate entire Ethernet (TCP/IP) frames.
- **New features:**
  - **VCID (Virtual CAN Network ID):** Splits one physical bus into multiple logical networks (like VLANs).
  - **SDT (Service Data Unit Type):** Upper-layer indicator describing the payload type.
  - **AF (Acceptance Field):** 32-bit field for content- or node-based addressing, separate from the 11-bit priority ID.

---

## Higher-Layer Protocols

Bare-metal CAN only provides OSI Layers 1 (Physical) and 2 (Data Link). Higher-layer protocols add addressing, multi-packet transmission, and network management:

- **SAE J1939** — Standard for heavy-duty vehicles. Forces node-based addressing onto CAN 2.0B's 29-bit ID by partitioning it into a Parameter Group Number (PGN, what the data is) and a Source Address (who sent it).
- **CANopen** — Popular in industrial automation, robotics, and motor controllers. Uses an Object Dictionary model with Process Data Objects (PDOs, real-time) and Service Data Objects (SDOs, configuration).
- **DeviceNet / UAVCAN (Cyphal)** — Specialized frameworks for industrial and aerospace applications.

---

*See also: [[can-hardware-basics]] for physical implementation, [[can-troubleshooting]] for debugging, [[can-addressing-for-etrike]] for our project's ID scheme.*
