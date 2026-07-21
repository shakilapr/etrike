# Handoff summary — etrike RT/SYS ESP32 + Control Toolkit

## Goal
Get **RT** and **SYS** ESP32-S3 **N16R8** modules working on **physical CAN** (both buses if possible), visible in Control Toolkit API. User wants simple bus smoke tests first, then restore full firmware.

## Hardware map
| Role | Port (typical) | USB MAC | Notes |
|------|----------------|---------|--------|
| **RT** | **COM9** | `80:b5:4e:c7:d0:34` | Low CAN = TWAI GPIO**5 TX / 4 RX** @ 500k; High = MCP2515 SPI (often **missing** on bench) |
| **SYS** | **COM5** | `80:b5:4e:c5:b9:4c` | Low CAN only, same TWAI pins |
| **CANalyst-II** | USB | — | CTK Real mode: **CH0=high, CH1=low** @ 500k |

Transceivers: SN65HVD230 / WCMCU-230 (CTX↔GPIO5, CRX↔GPIO4). Docs say if no frames, **swap CTX/CRX**; fake chips can RX but not TX.

## Repo roots
- Workspace: `E:\work\etrike`
- RT firmware: `rt-esp32/` (env `vehicle`, board `esp32-s3-devkitc-1-n16r8`)
- SYS firmware: `sys-esp32/` (same)
- **Minimal CAN smoke** (preferred next step): `can-test/` (`role_rt` / `role_sys`)
- API: `control-toolkit/backend` on **http://127.0.0.1:8001** (`/api/v1/...`)
- UI vite often on 5173; native SIL may be set for Computer mode

## What was fixed (keep these)
1. **RT reboot loop**  
   Stale `sdkconfig.vehicle` had **`CONFIG_FREERTOS_HZ=100`**.  
   `pdMS_TO_TICKS(5) → 0` → `xTaskDelayUntil` assert → boot loop.  
   **Must be 1000 Hz** (defaults + patched vehicle/bench sdkconfigs + `pio_patch_sdkconfig.py` / SYS `patch_sdkconfig.py`).

2. **N16R8 PSRAM**  
   8MB octal works; **80 MHz** often fails MSPI timing. Use **40 MHz**, disable hard **MEMTEST**, **IGNORE_NOTFOUND**.  
   Confirmed: `Found 8MB PSRAM`, `Speed: 40MHz`, flash 16MB Boya.

3. **RT defensive DelayUntil**  
   `ticks_ms_at_least_1()` in `rt-esp32/src/main.cpp`.

4. **RT no MCP**  
   Skip `tx_high` watchdog spam when high CAN absent (`g_high_can_present`).

5. **TWAI recovery**  
   Full reinstall on TX fail / bus-off (`can_driver_twai.cpp`, rate-limited in `send_can_low`).

6. **Rate-limited logs**  
   Command stale / SYS HB timeout no longer 100 Hz spam.

## What was observed (physical)
### API (Real / `bench_test` / canalystii)
- Adapter often **active** then **quiet**.
- At best, **low** bus only:
  - `0x7FE` SYS_HEARTBEAT ~10 Hz  
  - `0x011` SYS_SAFETY_STS ~5 Hz  
  - `0x110`, `0x600`, `0x7B9` (SEB req ~50–58 Hz)  
- **No RT frames** (`0x7FD`, `0x210`, `0x204`, etc.) ever seen live on CANalyst.

### RT serial (COM9) after N16R8 fix
- Boots: `Ready — 6 tasks`, TWAI OK, MCP fail (expected).
- Then: **`Low CAN TX failed … state=2 tec=128`** (bus-off / no ACK).
- Occasional 1 TX “ok” after recovery, then fail again.
- Implied **RX may work** (SYS HB timeout path) but **TX path broken** or **not on same bus as CANalyst/SYS**.

### SYS
- Once **TX’d successfully** on low bus (API proof).
- USB serial often **silent** (console was UART0; defaults updated to **USB Serial/JTAG**).
- Later went **quiet** (power/USB/unplug or bus death).  
- Full **vehicle** flash to COM5 **succeeded once** (`sys-esp32/_flash_com5.log`, ~256KB app).

### High bus
- MCP2515 not ready → RT high tasks skipped.
- CANalyst high mostly **unseen** / old residual counts.

## Smoke test (in progress — finish this first)
**Path:** `can-test/`  
**Firmware:** `src/main.cpp`  
- Phase0: `TWAI_MODE_NO_ACK` self-test (chip only)  
- Phase1: NORMAL, 200 ms TX  
  - **RT** `role_rt`: id **0x100**, payload `RT`+seq → COM9  
  - **SYS** `role_sys`: id **0x200**, payload `SY`+seq → COM5  
- Optional envs: `role_rt_swap` / `role_sys_swap` (`TWAI_SWAP_TX_RX=1` → TX=4 RX=5)

```text
cd E:\work\etrike\can-test
pio run -e role_rt  -t upload --upload-port COM9
pio run -e role_sys -t upload --upload-port COM5
```

**Pass criteria**
1. Serial: self-test TX OK; STAT `tx_ok` rising, `tec` low.  
2. Peer RX: each board logs the other’s id.  
3. API: low bus live `0x100` and/or `0x200` (note: unknown IDs may still appear in raw adapter path; check `/api/v1/status` rx_count and `/api/v1/state` if decoded).

**If smoke fails**
- Try `*_swap` envs (crossed CTX/CRX).  
- Check 120Ω termination (one each end; CANalyst term settings).  
- Swap WCMCU-230 modules (docs: flaky TX).  
- Confirm RT/SYS/CANalyst **low** share CAN_H/CAN_L/GND.

**If smoke passes**
Restore full firmware:
```text
cd E:\work\etrike\sys-esp32 && pio run -e vehicle -t upload --upload-port COM5
cd E:\work\etrike\rt-esp32  && pio run -e vehicle -t upload --upload-port COM9
```
Expect low: SYS `0x7FE/0x011` + RT `0x7FD/0x210` (and keep-alive `0x204` in Manual).  
High needs MCP wired and working.

## Important config flags / files
- `rt-esp32/sdkconfig.defaults` + `sdkconfig.vehicle`: FREERTOS 1000, SPIRAM 40M, N16R8  
- `sys-esp32/sdkconfig.defaults`: same + **USB Serial JTAG console**  
- `rt-esp32/platformio.ini`: `-D ETRIKE_RT_TWAI_SWAP_TX_RX=0` (normal pins)  
- Pin map architecture: TX=5, RX=4 both ECUs  

## CTK API cheatsheet
```text
GET http://127.0.0.1:8001/api/v1/status
GET http://127.0.0.1:8001/api/v1/state
GET http://127.0.0.1:8001/api/v1/sessions
GET http://127.0.0.1:8001/api/v1/sessions/profiles
```
Session was **`bench_test` / physical** when canalyst worked. Low `rx_count` climbing with live frames = good.

## Latest software push (2026-07-21)
- `can-test` enhanced with **auto seek** (`role_rt_seek`): cycles TX/RX pin maps × NORMAL/NO_ACK.  
- Flashed: **COM9** `role_rt_seek`, **COM5** `role_sys` (SUCCESS).  
- **API proof (when CANalyst was up):** low bus live **only 0x200 SYS smoke** (~5 Hz, payload `SY`+seq). **0x100 RT never live.**  
- **SYS serial:** NORMAL TX works (`tx_ok` climbing, few fails).  
- **RT serial:** receives many frames (`rx` 1000+); TX intermittent + bus-off even in NO_ACK seek; never LOCKED.  
- **Blocker now:** CANalyst-II **USB not found** (`reconnect N failed: No Canalyst-II USB device found`) — API cannot see frames until re-plugged.  

## Interrupted / incomplete work
- Full vehicle RT/SYS re-flash after dual-ID smoke **not done**.  
- High bus (MCP) **not working** on this bench.  
- RT stable TX still not achieved via software alone.

## Suggested next AI sequence
1. Kill stray `pio`; list COM5/COM9.  
2. Flash `can-test` `role_rt`→COM9, `role_sys`→COM5.  
3. 8–10 s serial on both + API low activity.  
4. On TX fail only: flash swap envs; retest.  
5. On pass: reflash full `vehicle` RT+SYS; confirm RT+SYS IDs on low.  
6. High: only after MCP present and SPI init OK.

## Root diagnosis so far (likely)
- **Software boot/PSRAM/tick rate:** fixed.  
- **RT low CAN TX:** hardware/wiring/transceiver/termination or wrong bus attachment (bus-off, no ACK) — **smoke test is the discriminator**.  
- **SYS:** can TX when powered/on bus; USB log/console was the main software friction.
