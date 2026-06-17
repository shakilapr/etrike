# CAN Troubleshooting — Common Mistakes & Debugging

Most beginner CAN failures are not caused by mysterious protocol bugs — they come from a small set of repeat offenders. This note covers how to identify and fix them, and the diagnostic tools to use.

> ⚠️ **Safety warning:** Don't connect an ESP32 (or any dev board) directly to a road vehicle's CAN bus unless you're absolutely certain what you're doing — you could disable critical systems and create safety risks. Use an OBD2 adapter or test on a bench first.

---

## Quick Debugging Checklist

Before posting a forum question or concluding your controller is defective, work through these:

1. **~60Ω across CAN_H/CAN_L?** (power off) — verifies two 120Ω terminators at bus ends
2. **Voltages sane?** CAN_H ≈ 2.5–3.5V, CAN_L ≈ 1.5–2.5V (powered, idle bus)
3. **Real transceiver on every node?** — not just a CAN-capable MCU pin
4. **Transceiver in normal mode?** — not standby or silent
5. **≥2 active nodes on the bus?** — unless in self-test/loopback
6. **CAN_H→CAN_H, CAN_L→CAN_L, shared GND?** — polarity and ground reference
7. **Linear trunk topology?** — no star, short stubs
8. **All nodes at same baud rate?** — and same sample point configuration
9. **Classical CAN working first?** — before enabling CAN FD
10. **Error counters checked?** — read TEC/REC before retrying forever
11. **ISR/foreground buffer ownership clear?** — no shared-buffer races

---

## Common Mistakes — Symptoms & Fixes

### Physical Layer

| Mistake | Symptoms | Fix |
|---------|----------|-----|
| **No transceiver** (MCU has controller only) | TXD toggles, CAN_H/CAN_L stay flat | Add ISO 11898-compatible transceiver to each node |
| **Transceiver in standby/silent** | Internal loopback works, bus silent | Drive STBY/S pin to normal mode |
| **Only one active node** | Tx FIFO fills, endless retransmissions, ACK errors | Use ≥2 active nodes, or internal loopback for self-test |
| **Missing/wrong termination** | Intermittent comms, worse at higher bitrates | Exactly two 120Ω at bus ends → measure ~60Ω |
| **Star topology / long stubs** | Works on bench, fails at speed or with more nodes | Linear trunk, stubs <30cm at 1 Mbps |
| **CAN_H/CAN_L swapped** | No comms, decode errors | Verify CAN_H→CAN_H, CAN_L→CAN_L |
| **No common ground** | Erratic errors, transceiver burnout | Run dedicated signal ground, or use galvanic isolation |

> ⚠️ **The "two-wire" label is a trap.** CAN is often advertised as a two-wire protocol (CAN_H and CAN_L). This is misleading. While signaling is differential, the transceivers need a common reference frame — otherwise voltages drift beyond the common-mode range and transceivers burn out or drop packets. If your nodes run on separate, isolated power supplies, you **must** run a third wire connecting their grounds (CAN_GND). Failing to tie grounds together is a primary cause of inexplicable failures in real-world setups. See [[can-hardware-basics#2-the-two-wire-myth-common-ground]].

### Configuration & Timing

| Mistake | Symptoms | Fix |
|---------|----------|-----|
| **Mismatched baud rates** | Error storms, no valid frames | All nodes use identical baud rate — compute from oscillator, don't guess |
| **Wrong sample point** | Marginal comms, occasional errors | Set sample point to ~75–87.5% of bit time (e.g., 14 of 16 time quanta) |
| **RC oscillator at ≥250 kbps** | Unreliable timing | Use quartz/crystal oscillator — RC accuracy may be insufficient |
| **CAN FD without data-phase config** | Classical works, FD fails | Configure nominal bitrate, data bitrate, and TDC/SSP explicitly |
| **Classical node on FD bus** | Error frames, bus-off | Keep all nodes Classical during bring-up; move to FD together |
| **Standard/Extended ID mismatch** | Sender "works," receiver never matches | Consistent 11-bit vs 29-bit choice across controller, filters, analyzer |

### Software

| Mistake | Symptoms | Fix |
|---------|----------|-----|
| **Treating CAN ID as node address** | Wrong priorities, awkward filter design | Design IDs around message meaning & priority; addressing is application-layer |
| **Wrong filter mask polarity** | Bus active but app sees no frames | Start accept-all, then narrow. Rule: mask bit 1 = compare, 0 = ignore |
| **Unsafe ISR/foreground buffer sharing** | Corrupted payload, zeros appearing, lost messages | ISR writes, foreground consumes — separate ownership via queue or double buffer |
| **Duplicate listeners / missing shutdown** | Duplicate callbacks, memory growth | `Notifier.stop()`, avoid adding same listener twice, use `ThreadSafeBus` in Python |

---

## Step-by-Step Fix Procedures

### Physical layer first — before touching software

1. Power the bus down. Measure CAN_H to CAN_L resistance → should be **~60Ω**. If not, fix termination and topology before anything else.
2. Verify every node has a real transceiver and it's in **normal mode** (not standby/silent).
3. Verify CAN_H→CAN_H, CAN_L→CAN_L, and GND are connected correctly.
4. Reduce to **two nodes only** on a short linear cable. This single procedure fixes "no waveform", "bus silent", "works in loopback only", and many "random" overrun reports.

### ACK errors / "nothing is transmitted"

Real CAN requires an ACK from another node. If you have only one transmitting node plus a passive analyzer, the controller keeps retrying forever and the Tx FIFO appears to "stick".

- Use internal loopback to confirm the controller block works.
- Connect a second **active** ACK-capable node, then retest in normal mode.
- Listen-only mode won't ACK — it can monitor but can't serve as the only partner in a two-device test.

### Baud rate & bit timing

- Pick one conservative Classical CAN bitrate. Make every node use identical nominal timing.
- Compute timing from oscillator and controller clock — don't tune by folklore. Apply identically to every node.
- Rule of thumb: **12–20 time quanta** with sample point at **75–87.5%** of bit time.

### CAN FD specific

- Make Classical CAN work first. Enable FD only after specifying **both** nominal bitrate and data-phase bitrate.
- If FD works only when slowed down or with BRS disabled: suspect wrong data timing or missing TDC (transceiver delay compensation) before suspecting payload format bugs.

### Classical vs FD mismatch

- Put every device on Classical CAN first. Once stable, move every live device to **ISO CAN FD** together. Only then tune data-phase bitrate.
- A Classical node on an FD bus will send error frames — it doesn't understand FD frames. Use a gateway if legacy Classical nodes must remain.

### Error frames / Error Passive / Bus Off

- An error frame is the *mechanism* by which CAN deletes a bad frame — not the "bug" itself.
- Fix the physical or timing cause, inspect counters (TEC/REC) and error status, then run the controller's documented recovery sequence instead of blindly hammering retransmissions.

### Message ID, masks & filters

- Separate protocol facts from project conventions. CAN ID = arbitration priority, not automatically a node address.
- Start with **accept-all** or passive monitoring. After traffic is proven good, add filters.
- Classic bit-mask rule: **mask bit 1 = compare this bit, mask bit 0 = ignore this bit.** All ones = exact match, all zeros = everything matches.
- On MCAN-style controllers, filters are checked in order; evaluation stops at the **first** match — ordering matters.

### Software: races, listeners, overflows

- **Linux SocketCAN:** Uses local loopback semantics — host-local visibility ≠ proof the bus is healthy. Supports `vcan` for testing without hardware.
- **python-can:** Filtering is often in hardware/kernel space, not Python space. Avoid adding the same listener twice. Use `ThreadSafeBus` for multi-threaded access. `Notifier.stop()` matters — listeners may flush state on shutdown.
- **Bare-metal/RTOS:** Never let ISR and foreground code write the same frame buffer without ownership rules. "CAN data became zero" is usually a race condition.

---

## Understanding Error States

CAN controllers maintain two counters: **TEC** (Transmit Error Counter) and **REC** (Receive Error Counter).

| State | Condition | Behavior |
|-------|-----------|----------|
| **Error Active** | TEC ≤ 127 and REC ≤ 127 | Normal operation; can send Active Error Flags |
| **Error Passive** | TEC > 127 or REC > 127 | Can still communicate but sends Passive Error Flags; waits longer between retransmissions |
| **Bus Off** | TEC > 255 | Node disconnects from bus; must be manually recovered |

**Key insight:** An Error Frame is the *mechanism* by which CAN deletes a bad frame — it's not the "bug" itself. Fix the root cause (physical, timing, or configuration), then recover the controller per its datasheet.

---

## Diagnostic Tools & Workflow

Use tools in this order: **multimeter → oscilloscope → CAN analyzer → logic analyzer → load tools.**

### 1. Multimeter (always first)

- Power off, measure CAN_H to CAN_L resistance → should be **~60Ω**
- Check continuity: CAN_H, CAN_L, GND on each connector

### 2. Oscilloscope

Ask four questions in order:
1. Does TXD toggle when software sends a frame?
2. Does RXD follow expected behavior?
3. Do CAN_H and CAN_L move differentially?
4. Do edges and idle levels look stable, or do they ring/distort?

**Key insight:** If TXD toggles but CAN_H/CAN_L stay flat → missing transceiver, transceiver in standby, missing power, or wrong pin mux.

### 3. CAN Analyzer (e.g., PCAN-View, Kvaser)

- Start in **listen-only** mode for passive observation — don't disturb an existing bus
- Configure the exact nominal bitrate before anything else
- Only after passive observation is sound should you use it as an active node
- Monitor bus load and error counters

> **Common trap:** USB-to-CAN connected ≠ another node ACKing you. Listen-only analyzers do not ACK.

### 4. Logic Analyzer

- Use only for the **controller/transceiver boundary** — probe TXD/RXD
- Do NOT use as a substitute for a proper differential CAN instrument
- If probing CAN_H/CAN_L directly, ensure the analyzer's electrical limits are appropriate

### 5. Bus Load & Replay Tools

- Linux: `cangen`, `candump`, `canplayer`, `cansequence` from `can-utils`
- Increase load gradually; watch for lost frames, overruns, sequencing issues
- `vcan` (virtual CAN) is excellent for testing logic without hardware

---

## Troubleshooting Flowchart

```
No CAN communication
    │
    ├─ Power off: ~60Ω between CAN_H & CAN_L?
    │   └─ No → Fix termination & bus topology
    │
    ├─ Real transceiver on every node, in normal mode?
    │   └─ No → Add transceiver or drive STBY/S to normal
    │
    ├─ CAN_H→CAN_H, CAN_L→CAN_L, GND shared?
    │   └─ No → Fix wiring & polarity
    │
    ├─ ≥2 active nodes to provide ACK?
    │   └─ No → Add second active node or use self-test loopback
    │
    ├─ Classical CAN works at low nominal bitrate?
    │   └─ No → Recalculate nominal bit timing & sample point
    │
    ├─ Using CAN FD?
    │   ├─ No → Add IDs, filters, interrupts, then raise load gradually
    │   └─ Yes → Nominal + data phase bit timing + TDC configured?
    │       └─ No → Set dbitrate, data sample point, TDC/SSP
    │
    ├─ Error counters rising / bus-off?
    │   └─ Yes → Read TEC/REC, inspect waveform, fix root cause, recover bus
    │
    └─ Check filters, std/ext ID choice, software races & queue overruns
```

---

## Minimal Debugging Code

### Linux SocketCAN (C) — includes error frame reporting

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main(void) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) return 1;

    // Subscribe to error frames for debugging
    can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT | CAN_ERR_BUSOFF;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    struct ifreq ifr = {0};
    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) return 2;

    struct sockaddr_can addr = {0};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) return 3;

    struct can_frame f = {0};
    f.can_id  = 0x123;
    f.can_dlc = 2;
    f.data[0] = 0xAA;
    f.data[1] = 0x55;

    if (write(s, &f, sizeof(f)) != sizeof(f)) return 4;
    close(s);
    return 0;
}
```

### C++ — ISR-safe double buffer pattern

```cpp
#include <array>
#include <atomic>
#include <cstdint>

struct CanFrame {
    uint32_t id{};
    uint8_t  dlc{};
    std::array<uint8_t, 8> data{};
};

static CanFrame isr_buffer;
static std::atomic<bool> frame_ready{false};

void can_rx_isr(const CanFrame& hw_frame) {
    isr_buffer = hw_frame;                          // ISR writes only
    frame_ready.store(true, std::memory_order_release);
}

bool try_fetch_frame(CanFrame& out) {
    if (!frame_ready.exchange(false, std::memory_order_acq_rel))
        return false;
    out = isr_buffer;                               // foreground consumes only
    return true;
}
```

> For sustained traffic, replace the single slot with a ring buffer or RTOS queue.

### Python (python-can) — with thread safety & filters

```python
import can

filters = [
    {"can_id": 0x123, "can_mask": 0x7FF, "extended": False},
]

bus = can.ThreadSafeBus(interface="socketcan", channel="vcan0", can_filters=filters)
notifier = can.Notifier(bus, listeners=[can.Printer()], timeout=0.2)

try:
    msg = can.Message(arbitration_id=0x123,
                      data=[0x01, 0x02, 0x03],
                      is_extended_id=False)
    bus.send(msg, timeout=0.5)
    rx = bus.recv(timeout=1.0)
    print("RX:", rx)
finally:
    notifier.stop()
    bus.shutdown()
```

> Use `vcan0` to test logic before involving real hardware.

---

## Reference Materials

### Official specifications
- ISO 11898-1 (protocol) and ISO 11898-2 (physical layer) — current editions: 2024 and 2026
- [Bosch CAN Protocols](https://www.bosch-semiconductors.com/ip-modules/can-protocols/) — primary source for CAN FD
- [Bosch CAN FD overview](https://www.bosch-semiconductors.com/ip-modules/can-fd-ip-modules/can-fd/)

### Vendor documentation
- Microchip AN754 — bit timing for CAN
- Microchip CAN mechanism overview, filtering, and error-handling pages
- NXP bit-timing guidance and calculator
- TI platform FAQ, MCAN debug/getting-started notes, and physical-layer debug article
- [Linux SocketCAN documentation](https://www.kernel.org/doc/html/latest/networking/can.html)
- [python-can API documentation](https://python-can.readthedocs.io/)

### Tools
- **CAN analyzers:** PCAN-View (PEAK), Kvaser, KJElectronics
- **Linux can-utils:** `cangen`, `candump`, `canplayer`, `cansequence`
- **Virtual CAN:** `vcan0` — test logic without any hardware
- **Oscilloscope** with CAN/CAN FD decoding (e.g., PicoScope)

### Books & specs
- Bosch CAN 2.0B specification
- CANopen DS301/DS303 (for higher-layer protocol reference)

---

*See also: [[can-protocol]] for protocol theory, [[can-hardware-basics]] for physical setup, [[can-addressing-for-etrike]] for our project's ID assignments.*
