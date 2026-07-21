# Handoff — RT/SYS ESP32-S3 N16R8 + Control Toolkit CAN

**Last updated:** 2026-07-21  
**Workspace:** `E:\work\etrike`  
**User intent:** Verify hardware with simple tests first; make both controllers publish/receive on both buses; only then run full vehicle firmware. Prefer API (`/api/v1`) to observe CAN.

---

## 1. Current hardware map (authoritative)

| Role | Serial port **now** | Chip MAC (USB/eFuse) | Notes |
|------|---------------------|----------------------|--------|
| **SYS** | **COM6** (CH343 USB-UART) | `80:b5:4e:c5:b9:4c` | User: CH343 → ESP UART pins |
| **RT** | **COM10** (CH343 USB-UART) | `80:b5:4e:c7:d0:34` | Same |
| **CANalyst-II** | USB (python-can) | — | CTK Real: **CH0 = high**, **CH1 = low**, 500 kbit/s |

**Earlier ports (when native USB-JTAG was used):** COM5 = SYS, COM9 = RT. Do **not** assume those; always list ports.

**ESP USB-JTAG** (VID `303A`) may or may not be plugged. With CH343 on UART0, **app `ESP_LOG` only appears if console is UART**. Builds that set `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` show only **ROM boot** on COM6/COM10 (entry to flash boot, then silence).

### CAN pinout (architecture)
| Bus | Controller | Pins |
|-----|------------|------|
| **Low** | Built-in TWAI | **TX=GPIO5, RX=GPIO4**, 500 kbit/s |
| **High** | MCP2515 SPI (RT only) | **SCK=15, MOSI=16, MISO=17, CS=18, INT=7** |

SYS has **no high CAN**. High needs RT MCP + CANalyst CH0 (+ host).

Transceivers: SN65HVD230 / WCMCU-230 @ 3.3 V. Docs: if no frames, try swap CTX/CRX; flaky modules can RX but not TX.

---

## 2. Goal / process agreed with user

1. **Hardware verify** with simple firmware before vehicle builds.  
2. Use **API** to check each bus and each controller publish/receive.  
3. Then flash full `vehicle` RT + SYS.  
4. High bus separately (MCP must answer SPI).

---

## 3. Services already running (often)

| Service | URL / path |
|---------|------------|
| CTK API | `http://127.0.0.1:8001` prefix `/api/v1` |
| Frontend vite | `http://127.0.0.1:5173` |
| Native SIL (optional) | `native-test/build-sil/sim_engine_native.exe` via env on backend |

Session typically: **`bench_test`**, destination **`physical`**, canalystii.

### Useful API
```http
GET  /api/v1/status
GET  /api/v1/state
GET  /api/v1/history?limit=50
GET  /api/v1/topology
GET  /api/v1/sessions
POST /api/v1/sessions/{id}/bench-tx   {"enabled": true, "expected_revision": N}
POST /api/v1/injections               {"bus":"high","key":"host:host_heartbeat","values":{...},"period_ms":500}
POST /api/v1/injections/raw           {"bus":"high","can_id":2044,"data_hex":"0100","confirm_raw":true}
```

Smoke IDs **0x100 / 0x200** are **not** in the protocol catalog → `key=None`, `freshness=invalid` is OK; still count as live if `age_ms` is low.

---

## 4. Software fixes already in tree (keep)

### Boot / N16R8
- **`CONFIG_FREERTOS_HZ=1000`** (was 100 → `pdMS_TO_TICKS(5)==0` → `xTaskDelayUntil` assert reboot).  
  Files: `rt-esp32`/`sys-esp32` `sdkconfig.defaults`, `sdkconfig.vehicle`, `pio_patch_sdkconfig.py` / `patch_sdkconfig.py`.
- **PSRAM octal 40 MHz**, no hard memtest, `IGNORE_NOTFOUND`.
- Board: `esp32-s3-devkitc-1-n16r8` (16 MB flash, 8 MB PSRAM). Confirmed on both chips.

### RT app
- `ticks_ms_at_least_1()` for DelayUntil.  
- Skip `tx_high` watchdog when MCP missing (`g_high_can_present`).  
- **Soft TWAI recovery** + mutex + **3 s debounce** (stop thrash that caused “works then dies” / spinlock crash).  
  Files: `rt-esp32/src/can_driver_twai.cpp`, `main.cpp` `send_can_low`, `can_health.h`.

### SYS app
- Same soft recovery + mutex + 3 s debounce.  
  Files: `sys-esp32/src/can_driver.h`, `main.cpp` diag recovery path.

### Console
- Defaults were flipped between USB-JTAG and UART for CH343.  
  **For COM6/COM10 UART logs:** `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (+ optional secondary USB-JTAG).  
  **For native USB-JTAG only:** USB-JTAG primary.  
  Current `sdkconfig.defaults` intent: UART primary for CH343 bench.

---

## 5. What works vs what doesn’t (latest observations)

### Low bus (CH1) — **works when SYS is up**
- Adapter **active**, low **active**, `rx_count` climbs (~60–70 frames/s when SYS vehicle or smoke TX).  
- **SYS vehicle IDs live and stable for 10–12 s+** (not dying immediately after fix window):
  - `0x7FE` SYS_HEARTBEAT  
  - `0x011` SYS_SAFETY_STS  
  - `0x110` SYS_MODE_CMD  
  - `0x600` SYS_DIAG_RPT  
  - `0x7B9` VCU_SEB_REQ (can be ~50 Hz flood)

### Smoke test (when both flashed successfully earlier)
- **SYS `0x200`** live on low (~5 Hz, payload `53 59 …` = `SY`+seq).  
- **RT `0x100`** also live on low when RT smoke was good (API proof once: both IDs for 12 s).  
- Later sessions often showed **only `0x200`** → RT not publishing or not flashed.

### High bus (CH0) — **not working as a peer bus**
- Host inject: **`tx_count` increases** (CANalyst can TX).  
- **`rx` stuck** (~2105), no live high frames in history.  
- RT vehicle log: **`MCP2515 not ready (CANSTAT=0x00)`** → SPI returns zeros → no MCP / wrong pins / no power.  
- High tasks skipped: `Ready — 6 tasks` (low-only).  
- SYS has no high controller.

### Serial on CH343 (COM6/COM10)
- Opening port often only shows **ROM**: `SPI_FAST_FLASH_BOOT` / `entry 0x…` then silence if console is USB-JTAG.  
- App is still running (API proves SYS CAN).  
- Do not conclude “upload failed” from silent CH343 if ROM boot appears after flash.

### “Works a few seconds then stops”
- Was **bus-off + recovery thrash** (full TWAI uninstall every 1 s from multiple tasks → spinlock).  
- Soft recovery + debounce **landed in source**; may not be on flash if vehicle rebuild interrupted.  
- Steady SYS heartbeats on API after later flashes suggest SYS path improved when bus + image OK.

---

## 6. Flash / upload status (important for next AI)

Uploads were **often interrupted** (session cancel / long ESP-IDF rebuild / port busy). That is **not** “COM broken.”

| Attempt | Result |
|---------|--------|
| SYS smoke → **COM6** | **SUCCESS** (hash verified, MAC `…:c5:b9:4c`) |
| RT smoke → **COM10** | **Often incomplete** (build of `role_rt` still compiling when job killed) |
| Vehicle SYS/RT → COM6/COM10 | **Failed/interrupted** mid-rebuild (`SYS_FAIL` once) |
| Earlier vehicle → COM5/COM9 | SYS and RT vehicle succeeded at least once on native USB ports |

**Right now (API snapshot while writing this doc):**
- Low **active**, live **`0x200` only** (SYS smoke likely still running).  
- No RT id.  
- High **quiet**.

---

## 7. Tooling: `can-test/` (use this first)

Path: `E:\work\etrike\can-test`

| Env | Purpose |
|-----|---------|
| **`role_sys`** | SYS: TX **0x200** @ 200 ms, payload `SY`+seq, NORMAL TWAI |
| **`role_rt`** | RT: TX **0x100** @ 200 ms, payload `RT`+seq |
| **`role_rt_seek`** | RT auto-cycles pin map × NORMAL/NO_ACK |
| **`role_*_swap`** | TX/RX GPIOs swapped (4/5) |
| **`hw_verify`** | PSRAM + TWAI self-test + MCP SPI probe (primary + legacy pins) + loopback |
| **`spi`** | MCP SPI-only (pins fixed to 15–18/7) |

**ESP-IDF note:** PlatformIO `build_src_filter` is **ignored**. Sources chosen in `src/CMakeLists.txt` via `$ENV{PIOENV}` or `board_build.cmake_extra_args = -DAPP_SRC=...`.

```powershell
cd E:\work\etrike\can-test
# Kill stray pio first
Get-Process pio -ErrorAction SilentlyContinue | Stop-Process -Force

pio run -e role_sys -t upload --upload-port COM6
pio run -e role_rt  -t upload --upload-port COM10

# Then API watch for low:0x100 and low:0x200 for ≥15 s
```

**Pass (low smoke):**
1. Both IDs live (`age_ms` < ~500) continuously.  
2. History shows both IDs.  
3. Optional: serial RX of peer id if UART console enabled.

**High smoke:** only after `hw_verify` shows MCP SPI + loopback PASS on RT. Then vehicle RT can TX high; host inject alone does not prove high peer bus.

Docs: `control-toolkit/docs/HARDWARE-VERIFY.md`

---

## 8. Vehicle flash (only after smoke)

```powershell
cd E:\work\etrike\sys-esp32
pio run -e vehicle -t upload --upload-port COM6

cd E:\work\etrike\rt-esp32
pio run -e vehicle -t upload --upload-port COM10
```

Expect low: SYS `0x7FE/0x011` + RT `0x7FD/0x210` (+ `0x204` keep-alive).  
High: only if MCP ready.

---

## 9. Root causes (current best model)

| Layer | Status |
|-------|--------|
| ESP modules (CPU/PSRAM/flash) | **OK** on both (esptool + PSRAM logs) |
| FREERTOS_HZ / DelayUntil reboot | **Fixed in tree** |
| Low bus + SYS publish | **Works** (stable API) |
| RT low publish | **Intermittent / often missing** — need confirmed `role_rt` flash + stable TX |
| Recovery thrash “stops after seconds” | **Fixed in tree**, re-flash vehicle to deploy |
| High bus | **MCP not detected** (`CANSTAT=0x00`) — hardware/SPI, not CTK |
| CH343 silent app logs | Console on USB-JTAG vs UART0 — config/wiring, not dead MCU |
| Upload “not working” | Long rebuilds + cancelled jobs + port busy — retry **upload-only** when `.bin` exists |

---

## 10. Next AI checklist (do in order)

1. **List ports** — confirm COM6 = SYS, COM10 = RT (or update map).  
2. **Kill all `pio`** processes; one upload at a time.  
3. **Smoke:**  
   - `role_sys` → COM6 (may already be done).  
   - Finish **`role_rt` → COM10** (build if needed, then upload).  
4. **API 15–30 s:** require **both** `0x100` and `0x200` live; `dL` climbing.  
5. If only `0x200`: RT power/bus/TX; try `role_rt_swap`; check serial if UART console.  
6. **RT `hw_verify` → COM10:** read SUMMARY for MCP.  
7. If MCP FAIL: stop high work; fix SPI/power.  
8. If low smoke solid: flash vehicle SYS+RT with recovery fixes; watch `0x7FE` + `0x7FD`.  
9. High: enable bench-tx, inject host HB on high; expect RT high only after MCP PASS.  
10. Do **not** thrash fullclean unless sdkconfig console/PSRAM change requires it.

---

## 11. Key file paths

```
control-toolkit/HANDOFF-rt-sys-can.md          ← this file
control-toolkit/docs/HARDWARE-VERIFY.md
can-test/src/main.cpp                         ← smoke dual-role
can-test/src/hw_verify.cpp                    ← hardware verify
can-test/platformio.ini
rt-esp32/src/can_driver_twai.cpp              ← soft recovery + mutex
rt-esp32/src/main.cpp                         ← send_can_low debounce
rt-esp32/src/can_health.h
rt-esp32/src/config.h                         ← TWAI + MCP pins
sys-esp32/src/can_driver.h
sys-esp32/src/main.cpp
```

---

## 12. One-liner status for next session

**SYS on COM6 is publishing smoke/vehicle traffic on low CAN; CANalyst low is active. RT on COM10 often not on the bus (smoke upload incomplete). High bus has no peer (MCP silent). Prefer finishing dual smoke flash on COM6/COM10 and API-checking 0x100+0x200 before vehicle firmware.**
