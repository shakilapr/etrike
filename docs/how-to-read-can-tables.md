# How to Read CAN Signal Tables

Every CAN message (frame) carries up to 8 payload bytes. Each byte is split into one or more **signals** — the individual values packed into the frame.

## Column Reference

| Column | Meaning |
|--------|---------|
| **Signal** | Name of the signal within the message |
| **Byte** | Byte index within the CAN payload (0‑based — 0 = first data byte) |
| **Bit** | Bit offset within that byte (0 = LSB, 7 = MSB) |
| **Len** | Signal width in bits |
| **Type** | Signed (two's complement) or unsigned integer |
| **Scale** | Formula to convert raw CAN value to physical units: `physical = raw × factor + offset` |
| **Unit** | Physical unit after scaling (`—` = unitless / boolean flag) |
| **Values** | Enumeration mapping when the raw value encodes a discrete state (`0=Off, 1=On`) |

## Key Terms

**DLC** — Data Length Code. The number of payload bytes in the CAN frame (0–8). A DLC=0 frame carries no payload data — the CAN ID itself is the signal (used for ESTOP).

**Frame Layout** — The ASCII diagram below each message header shows which bits are occupied (`#`) vs unused (`.`). Multi-byte signals show a `↳` marker on continuation bytes.

**Cycle / Event** — How often the frame is sent. A cycle value (e.g., `100ms`) means periodic transmission. `Event` means sent only on state change.

**Sender → Receivers** — Which ECU transmits the frame and which ECUs consume it.

## Example

```
0x300 — HOST_DRIVE_CMD
DLC=8 | 10ms | Host → RT

  [B0] # # # # # # # #  HOST_DriveSpeed       ← 32-bit signed, bytes 0–3
  [B1] # # # # # # # #  ↳ HOST_DriveSpeed
  [B2] # # # # # # # #  ↳ HOST_DriveSpeed
  [B3] # # # # # # # #  ↳ HOST_DriveSpeed
  [B4] # # # # # # # #  HOST_YawRate          ← 24-bit signed, bytes 4–6
  [B5] # # # # # # # #  ↳ HOST_YawRate
  [B6] # # # # # # # #  ↳ HOST_YawRate
  [B7] # # # # # # # #  HOST_Gear             ← 8-bit unsigned, byte 7

| Signal          | Byte | Bit | Len | Type     | Scale | Unit | Values        |
| HOST_DriveSpeed | 0    | 0   | 32  | signed   | 1     | mm/s | —             |
| HOST_YawRate    | 4    | 0   | 24  | signed   | 1     | mrad | —             |
| HOST_Gear       | 7    | 0   | 8   | unsigned | 1     | enum | 0=N,1=D,2=S,3=R |
```

To read `HOST_DriveSpeed`: take the 32-bit big-endian value from bytes 0–3, multiply by 1, result is speed in mm/s.
