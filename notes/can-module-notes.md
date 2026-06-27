# CAN Module Notes

Practical things learned from using these modules. Not a datasheet — just what matters when you wire them up and things go wrong.

---

## WCMCU-230

Tiny ESP32 board with an onboard CAN transceiver. Uses the ESP32's built-in TWAI controller — no external CAN chip.

### What it is

An ESP32 dev board with a CAN transceiver soldered on. The ESP32 talks CAN natively through its TWAI (Two-Wire Automotive Interface) peripheral. The transceiver converts 3.3V logic-level TX/RX into the differential CANH/CANL pair. No SPI, no external controller chip — direct memory-mapped registers inside the ESP32.

### Connection

**Logic side (4 wires):**
- `CTX` → TWAI TX pin (usually GPIO5 on ESP32-S3)
- `CRX` → TWAI RX pin (usually GPIO4)
- `VCC` → 3.3V
- `GND` → ground

**Bus side (3 screw terminals):**
- `CAN_H` → bus high (twisted pair)
- `CAN_L` → bus low (twisted pair)
- `GND` → bus ground reference

The ground reference between all nodes on a CAN bus matters — skipping it causes random bit errors that look like bus noise.

### TX/RX Pins

These must be GPIOs the ESP32 TWAI controller can actually use. On ESP32-S3 the standard assignment is GPIO4 for RX and GPIO5 for TX. Don't confuse them with UART TX/RX which are labeled the same but on different GPIOs (GPIO43/44 on the J3 header).

### Power

Runs fine at 3.3V. No 5V needed. Current draw is ~30mA — can be powered directly from a dev board's 3V3 pin.

### Crystal / Clock

No external crystal. TWAI uses the ESP32's internal clock. Bit timing is configured entirely in software via `twai_timing_config_t`. This eliminates the "wrong crystal" problem entirely — if your timing values are right, the bus is right.

### Termination

120Ω jumper on the board (two pins with a removable shunt). Enable if this node is at one end of the bus. Disable if it's in the middle. A CAN bus needs exactly two terminators.

### Initialization

With ESP-IDF: `twai_driver_install()` with your config, then `twai_start()`. If you ever need to reinitialize (e.g., after bus-off recovery), call `twai_driver_uninstall()` first. Calling `twai_driver_install()` while already installed returns `ESP_ERR_INVALID_STATE` silently — took a while to figure out why bus-off recovery never worked.

### Self-Reception

TWAI has a `tx.ss` flag. When set to 1 with an accept-all filter, every frame you transmit comes back into your own RX buffer. For a CAN gateway forwarding ESTOP frames, this creates an infinite loop — you forward an ESTOP, receive your own forwarded copy, forward it again. Set to 0 in production.

### Error Counters

Read via `twai_get_status_info()`. TEC > 255 means bus-off — the peripheral stops participating. You need to detect this and reinitialize. It won't recover on its own.

### When to use this module

Good for any ESP32 CAN node. Simple wiring, cheap, no crystal issues. Bad for CAN FD (TWAI is CAN 2.0 only) or when you need sophisticated hardware acceptance filtering.

---

## MCP2515 SPI Module

Blue board with two chips: MCP2515 (CAN controller) and TJA1050 (transceiver). Talks to your MCU over SPI.

### What it is

A standalone CAN controller. Handles everything — framing, CRC, arbitration, error detection, automatic retransmission. You read and write its registers over SPI. The transceiver converts the controller's logic-level signals to differential CANH/CANL.

Use it when: your MCU has no built-in CAN, or you need a second CAN bus (ESP32 only has one TWAI), or you want better hardware acceptance filtering than the built-in controller provides.

### Connection — Logic Side

Six wires to your MCU:
- `SCK` — SPI clock
- `SI` (MOSI) — data from MCU to MCP2515
- `SO` (MISO) — data from MCP2515 to MCU
- `CS` — chip select, active low. Can be hardware-managed by your SPI driver.
- `INT` — interrupt output, active low. Pulls low when a frame arrives or error occurs. Wire to an interrupt-capable GPIO. Without it you're polling.
- `VCC` — **5V**. The TJA1050 transceiver needs 5V to produce proper CAN bus voltage levels. At 3.3V the differential voltage is marginal and fails under bus load.
- `GND` — ground

### Connection — Bus Side

Three screw terminals: CANH, CANL, and sometimes a ground reference. Standard CAN bus rules apply.

### The Crystal Problem

Every MCP2515 module has a metal-can crystal. It's either 8 MHz or 16 MHz. They look identical. The chip uses this to derive CAN bit timing. If your code assumes 8 MHz and the module has 16 MHz, your bus runs at double speed and nothing works.

**How to check:** The crystal has tiny laser etching — "8.000" or "16.000". If you can't read it, try both configurations in Listen-Only mode. The one that actually receives frames is correct.

If you need to support 16 MHz, change the BRP (Baud Rate Prescaler) in CNF1. For 8 MHz: BRP=0 gives TQ=250ns. For 16 MHz: BRP=1 gives the same TQ=250ns. The rest of the timing (PropSeg, PS1, PS2) stays the same.

### SPI Details

- Mode 0 (CPOL=0, CPHA=0) or Mode 1 (CPOL=1, CPHA=1). Both work.
- Clock up to 10 MHz. 8 MHz is a safe default.
- MCP2515 is always the slave. MCU is the master.
- CS asserted low before every transaction, deasserted high after. If CS stays low between commands, the MCP2515 gets confused and ignores subsequent commands.
- MOSI and MISO are the most commonly swapped wires. If init fails, swap them first.

### Interrupts

The INT pin fires on falling edge when a CAN frame arrives (or on error, if error interrupts are enabled). The sequence after INT fires:

1. Read `CANINTF` register to see what happened (RX0IF or RX1IF)
2. Read the frame from the RX buffer
3. **Clear the interrupt flag** by writing 0 to that bit in `CANINTF`

Step 3 is the one people miss. If you don't clear the flag, INT stays low permanently. No more interrupts fire. The module appears "locked up." Write 0 to clear — writing 1 does nothing on this chip.

### TX and RX Buffers

**Three TX buffers (TXB0, TXB1, TXB2).** They have independent priority. TXB2 has highest default priority. If you have safety-critical frames (like ESTOP), put them in TXB2. Telemetry in TXB0. This gives ESTOP queue-jumping without needing CAN ID arbitration.

Only one TX buffer is needed for simple use. But using all three prevents blocking while waiting for a previous transmission to complete.

**Two RX buffers (RXB0, RXB1).** RXB0 has higher priority. A message matching both goes to RXB0. If RXB0 is full and RXB1 has rollover enabled, overflow goes to RXB1.

### Acceptance Filters

The MCP2515 filters incoming frames in hardware before interrupting your MCU. Two masks (RXM0, RXM1) and six filters (RXF0-RXF5). Logic:

```
Accept if: (Frame_ID & Mask) == (Filter & Mask)
```

For a gateway that needs everything: masks = 0x00 (accept all). For a node that only wants specific IDs: masks = 0x7FF and filters = your target IDs. Reduces interrupt load dramatically on busy buses.

RXB0 uses RXM0 + RXF0/RXF1. RXB1 uses RXM1 + RXF2-RXF5.

### Listen-Only Mode

The MCP2515 monitors the bus without ACKing anything. Doesn't participate — just listens. Uses: verify bit timing (if you see frames, it's right), sniff traffic safely, check what IDs are present before configuring filters.

Enter by setting REQOP=0b011 in CANCTRL. Verify OPMOD reads back 0b011.

### Initialization Sequence

Must be done in this exact order:

1. **Reset** (SPI command 0xC0). Forces Configuration mode.
2. **Verify** CANSTAT shows OPMOD=100 (Configuration). If not, SPI isn't working.
3. **Configure** CNF1/CNF2/CNF3 for your bitrate + crystal.
4. **Set up** masks and filters.
5. **Enable** interrupts in CANINTE.
6. **Switch to Normal** mode (REQOP=000).
7. **Verify** CANSTAT shows OPMOD=000. If not, you have a bus problem.

Skipping the verification steps means you might spend hours debugging a module that's silently sitting in Configuration mode doing nothing.

### Sending a Frame

1. Check TXBnCTRL.TXREQ is 0 (buffer not busy)
2. Write ID to TXBnSIDH/TXBnSIDL
3. Write DLC to TXBnDLC
4. Write data bytes to TXBnD0-TXBnD7
5. Set TXREQ=1 to start transmission

The MCP2515 handles arbitration and retransmission. If it loses arbitration, it retries automatically. If it hits an error, it retries. You just set TXREQ and wait.

### Error States

TEC and REC in registers 0x1C and 0x1D.
- TEC or REC > 96 → Error-Passive (still talks but can't flag errors)
- TEC > 255 → Bus-Off (disconnects from bus entirely)

Bus-off requires explicit reset and reinitialization. The chip won't recover on its own.

### Power Notes

The TJA1050 transceiver needs 5V for proper CAN bus voltage levels. Some modules have an onboard 3.3V regulator and accept 5V input. Others don't and must be powered at exactly 5V. If your system is 3.3V-only, look for modules with MCP2562 or SN65HVD230 transceivers instead — those work at 3.3V.

### Common Issues

- **MOSI/MISO swapped** — most common wiring error. Swap them before debugging anything else.
- **INT flags not cleared** — module appears to lock up after receiving a few frames. The flags must be cleared after every read.
- **Crystal mismatch** — works in Listen-Only but not Normal mode (or vice versa), or receives garbled frames. Check the crystal marking.
- **Missing ground reference** — random CRC errors. Connect CAN bus ground between all nodes.
- **CS not deasserted** — MCP2515 ignores subsequent commands. Make sure CS goes high between transactions.
- **Wrong mode** — chip is in Configuration mode instead of Normal. Verify CANSTAT OPMOD bits after switching modes.
