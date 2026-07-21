# Hardware verify before vehicle firmware

Do **not** flash full `rt-esp32` / `sys-esp32` vehicle builds until this passes.

## “Works a few seconds then stops”

That is almost always **CAN bus-off**, not a dead ESP:

1. Node TX fails (no ACK / noise / bad term) → TEC rises → **bus-off**.
2. Old firmware **reinstalled TWAI every 1 s** from multiple tasks → races → spinlock crash or silent stop.
3. **Software fix (2026-07-21):** soft recovery + mutex + 3 s debounce (RT + SYS). Still need a clean bus for steady TX.

Use `hw_verify` first; then vehicle with the new recovery code.

## Ports
| Board | Port | MAC |
|-------|------|-----|
| RT | COM9 | `80:b5:4e:c7:d0:34` |
| SYS | COM5 | `80:b5:4e:c5:b9:4c` |

## Flash verify firmware

```powershell
cd E:\work\etrike\can-test

# RT (has MCP2515 for high bus)
pio run -e hw_verify -t upload --upload-port COM9
# Serial monitor (or python serial read) — look for SUMMARY

# SYS (low only — MCP will FAIL; that is expected)
pio run -e hw_verify -t upload --upload-port COM5
```

## What PASS means

| Check | RT | SYS |
|-------|----|-----|
| PSRAM | PASS | PASS |
| TWAI NO_ACK self-test | PASS | PASS |
| TWAI NORMAL TX (needs peer/CANalyst ACK) | PASS if low bus wired | same |
| MCP2515 SPI present | **PASS required for high** | FAIL OK |
| MCP loopback | **PASS required for high** | n/a |

## After PASS

```powershell
cd E:\work\etrike\sys-esp32
pio run -e vehicle -t upload --upload-port COM5
cd E:\work\etrike\rt-esp32
pio run -e vehicle -t upload --upload-port COM9
```

## If MCP FAIL (CANSTAT 0x00)

High bus cannot work. Check:
- MCP2515 VCC 3.3V + GND
- SPI: SCK=15, MOSI=16, MISO=17, CS=18, INT=7
- Crystal on MCP module
- Verify firmware also tries legacy map 36/37/38/39/40 — if that PASSes, update `rt-esp32/src/config.h` pins
