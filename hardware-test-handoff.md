# Hardware Bench Test & Firmware Gateway Comprehensive Handoff

**Last Updated:** 2026-09-15  
**Workspace:** `e:\work\etrike`  
**Bench Hardware:** CANalyst-II Dual-Channel USB-CAN Adapter, Physical ESP32-S3 RT controller, Physical ESP32-S3 SYS controller  
**Controllers Under Test (Both Connected & Flashed):**  
- **RT Controller (`rt-esp32`):** `COM6` (ESP32-S3 DevKit, flashed with `hardware_bench`)  
- **SYS Controller (`sys-esp32`):** `COM10` (ESP32-S3 DevKit, flashed with `hardware_bench`)  
**Actuators NOT Connected:** `mtr` (Motor STM32), `ses` (Steering by Wire), `seb` (Electronic Brake).  

---

## 1. Core Testing Strategy & Strict Directives

### Directives from User (DO NOT DEVIATE)
1. **NO FAKING ACTUATORS (MTR / SES / SEB):**
   - **Do NOT simulate or feed mock MTR, SES, or SEB feedback packets over CAN.**
   - Do NOT inject synthetic `0x206 MTR_MOTOR_FBK`, `0x201 SES_STATUS`, or `0x721 / 0x731 SEB_STATUS`.
   - The user explicitly rejected synthetic actuator mocks: the system operates in **developer bypass mode** (`ETRIKE_SYSTEM_RUN_MODE=1` / `hardware_bench`), meaning missing actuator peer feedback (`g_bypass_mtr_absent`, `g_bypass_seb_sync`, `g_bench_solo_mode`) must NOT trigger safety stops.
   - The primary purpose of the bench test is to verify the **output commands** produced by SYS and RT intended for those actuators:
     - `0x204 RT_DRIVE_CMD` (Speed setpoint & gear sent from RT to MTR)
     - `0x205 RT_BRAKE_CMD` (Brake pressure intent sent from RT to SYS)
     - `0x169 VCU_SES_REQ` (Steering command sent from RT to SES)
     - `0x7B9 VCU_SEB_REQ` (Final brake stroke/pressure command sent from SYS to SEB)
     - `0x110 SYS_MODE_CMD` (Operating mode sent from SYS)
     - `0x113 SYS_PWR_CMD` (Contactor/power state sent from SYS)
     - `0x011 SYS_SAFETY_STS` (Vehicle safety & lighting status sent from SYS)
2. **Current Board Connections & Hardware Ports:**
   - **RT Node:** Connected via USB on **`COM6`** and dual CAN (MCP2515 High bus, TWAI Low bus).
   - **SYS Node:** Connected via USB on **`COM10`** and TWAI Low bus.
   - Physical `kEstopGpio` (GPIO 1) on SYS is **physically grounded** (LOW = NC switch closed / healthy).
3. **PlatformIO Environment:**
   - Both nodes MUST be compiled and uploaded using the **`hardware_bench`** environment:
     - `pio run -d rt-esp32 -e hardware_bench -t upload --upload-port COM6`
     - `pio run -d sys-esp32 -e hardware_bench -t upload --upload-port COM10`
   - Never use `vehicle` on the bench as it requires physical sensors that float without the vehicle harness.

---

## 2. Hardware & Bus Topology

```
+-------------------------------------------------------------------------+
|                              HOST PC                                    |
|   control-toolkit backend (FastAPI / Uvicorn on port 8001)              |
|   CANalyst-II Dual-Channel USB Adapter                                  |
+-------------------------------------------------------------------------+
          | Ch 0 (High Bus - 500k)           | Ch 1 (Low Bus - 500k)
          |                                  |
+---------------------+                      |
|      rt-esp32       |                      |
|  (ESP32-S3 DevKit)  |                      |
|  COM6 (USB to PC)   |                      |
| High: MCP2515 (SPI) |                      |
| Low:  TWAI (ESP32)  |<====================>|
+---------------------+       Low Bus        |
                              (TWAI)         |
                                             |
                                  +---------------------+
                                  |      sys-esp32      |
                                  |  (ESP32-S3 DevKit)  |
                                  |  COM10 (USB to PC)  |
                                  | Low: TWAI (ESP32)   |
                                  | GPIO 1 Grounded     |
                                  +---------------------+
```

### CAN Bus Split
- **High Bus (Ch 0, 500 kbps):** Connects Host PC (Jetson / CANalyst-II) $\leftrightarrow$ RT MCP2515.
  - Telemetry: `0x121 RT_MOTION_RPT` (100 Hz), `0x210 RT_STATE_RPT` (10 Hz), `0x501 RT_NODE_STATUS` (10 Hz), `0x7FD RT_HEARTBEAT` (2 Hz), `0x620 RT_DIAG_RPT` (1 Hz).
  - Forwarded from Low: `0x011 SYS_SAFETY_STS` (5 Hz), `0x500 SYS_NODE_STATUS` (5 Hz), `0x600 SYS_DIAG_RPT` (1 Hz).
  - Host Commands: `0x300 HOST_DRIVE_CMD`, `0x111 HMI_PWR_REQ`, `0x112 HMI_MODE_REQ`, `0x114 HOST_ESTOP_RESET_REQ`, `0x7FC HOST_HEARTBEAT`, `0x001 SAFETY_ESTOP`.
- **Low Bus (Ch 1, 500 kbps):** Connects RT TWAI $\leftrightarrow$ SYS TWAI (and future MTR, SES, SEB).
  - SYS Broadcasts: `0x7FE SYS_HEARTBEAT` (10 Hz), `0x110 SYS_MODE_CMD` (10 Hz), `0x113 SYS_PWR_CMD` (10 Hz), `0x011 SYS_SAFETY_STS` (5 Hz), `0x500 SYS_NODE_STATUS` (5 Hz), `0x600 SYS_DIAG_RPT` (1 Hz).
  - RT Broadcasts: `0x204 RT_DRIVE_CMD` (100 Hz), `0x501 RT_NODE_STATUS` (50 Hz), `0x210 RT_STATE_RPT` (10 Hz), `0x7FD RT_HEARTBEAT` (2 Hz), `0x169 VCU_SES_REQ` (50 Hz when active), `0x205 RT_BRAKE_CMD` (50 Hz when active).

---

## 3. Verified System Milestone (Live Hardware Confirmed)

Both boards were flashed with current `hardware_bench` firmware and booted cleanly.

### Live Node Statuses via `/api/v1/status`:
```json
{
  "estop": {
    "active": false,
    "host_latch": false,
    "nodes": {
      "sys": {
        "state": "STANDBY",
        "estop_active": false,
        "estop_latched": false,
        "recovery_pending": false,
        "ready": true,
        "degraded": false,
        "output_enabled": true,
        "block_mask": 0
      },
      "rt": {
        "state": "STANDBY",
        "estop_active": false,
        "estop_latched": false,
        "recovery_pending": true,
        "ready": true,
        "degraded": false,
        "output_enabled": false,
        "block_mask": 0
      }
    }
  }
}
```

### Verified Live Telemetry Stream:
- **`sys:sys_safety_sts` (0x011):** `estop_active: 0`, `heartbeat_ok: 1`, `light_brake: 0`. Forwarded Low $\rightarrow$ High by RT.
- **`sys:sys_pwr_cmd` (0x113):** `power_state: 1` (Contactor / Power ON).
- **`sys:sys_mode_cmd` (0x110):** `mode: 0` (MANUAL).
- **`rt:rt_node_status` (0x501):** `node_state: 2 (STANDBY)`, `block_mask: 0`, `ready: 1`.
- **`sys:sys_node_status` (0x500):** `node_state: 2 (STANDBY)`, `block_mask: 0`, `ready: 1`.
- **`rt:rt_drive_cmd` (0x204):** streaming at 104 Hz.
- **`rt:rt_motion_rpt` (0x121):** streaming at 99 Hz.

---

## 4. Backend Architecture & API Cookbook

The backend server runs from `e:\work\etrike\control-toolkit\backend` via:
```powershell
python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001 --log-level info
```

> [!WARNING]
> Only ONE process can open the CANalyst-II USB device at a time. The uvicorn server owns it. Never run external scripts trying to instantiate `can.Bus(interface='canalystii')` directly, or Windows will throw `[Errno 13] Access denied`. Always interact via the REST API at `http://127.0.0.1:8001`.

### Activating Bench TX Sessions
Before injecting CAN frames from the Host, the session must have TX enabled.
1. Start/Update Session:
   - Endpoint: `POST http://127.0.0.1:8001/api/v1/sessions`
   - Payload:
     ```json
     {"profile": "bench_test", "enable_bench_tx": true}
     ```
2. Enable Bench TX Directly (if already in session):
   - Endpoint: `POST http://127.0.0.1:8001/api/v1/sessions/bench_tx`
   - Payload: `{"enabled": true}`

### Injection Endpoints
- **Single Shot Injection:** `POST http://127.0.0.1:8001/api/v1/injections`
  ```json
  {
    "bus": "high",
    "key": "hmi:hmi_mode_req",
    "values": {"mode_req": 1, "rolling_counter": 1}
  }
  ```
- **Periodic Stream Injection:** `POST http://127.0.0.1:8001/api/v1/injections`
  ```json
  {
    "bus": "high",
    "key": "host:host_drive_cmd",
    "period_ms": 20.0,
    "values": {"speed_mmps": 1000, "yaw_rate_mrad_s": 0}
  }
  ```
- **Raw CAN Frame Injection:** `POST http://127.0.0.1:8001/api/v1/injections/raw`
  ```json
  {
    "bus": "high",
    "can_id": 1,
    "data_hex": ""
  }
  ```
- **Stop All Injections:** `DELETE http://127.0.0.1:8001/api/v1/injections`

---

## 5. Next Steps: Testing Outputs Without Actuators

Now that both nodes are in `STANDBY` with `estop_active = 0` and power ON (`power_state = 1`), execute the following tests:

### Step 1: Transition to AUTO Mode
1. Ensure session is active: `POST /api/v1/sessions` (`profile: bench_test`, `enable_bench_tx: true`).
2. Inject 3 sequential frames of `hmi:hmi_mode_req` on High bus (or Low bus):
   ```python
   for ctr in [1, 2, 3]:
       inject("high", "hmi:hmi_mode_req", {"mode_req": 1, "rolling_counter": ctr})
       time.sleep(0.02)
   ```
3. **Verify:**
   - SYS emits `0x110 SYS_MODE_CMD` with `mode: 1` (AUTO).
   - RT receives `0x110` and transitions `m_current_mode` to `1` (AUTO).
   - RT reports `mode: 1` in `0x210 RT_STATE_RPT` and `output_enabled: true` in `0x501 RT_NODE_STATUS`.

### Step 2: Feed Drive Commands & Check Actuator Outputs
Once in AUTO mode, stream `0x300 HOST_DRIVE_CMD` (e.g. 1000 mm/s forward, steer angle 10 deg) on High bus at 50 Hz (`period_ms: 20.0`):
1. **Verify RT Actuator Outputs on Low Bus:**
   - `0x204 RT_DRIVE_CMD`: `motor_speed_mmps` ramps to 1000 mm/s, `gear` becomes `D` (1).
   - `0x169 VCU_SES_REQ`: Steering angle command output updates with commanded angle.
   - `0x205 RT_BRAKE_CMD`: Brake pressure intent (0 kPa during drive, ramps when commanded).
2. **Verify SYS Actuator Output on Low Bus:**
   - `0x7B9 VCU_SEB_REQ`: SYS outputs brake commands to SEB according to brake intent.

### Step 3: Safety Trip & Recovery Verification
1. Assert remote ESTOP: Inject `0x001 SAFETY_ESTOP` or call `/api/v1/hmi/estop`.
2. Verify:
   - SYS enters ESTOP (`0x011` reports `estop_active = 1`).
   - RT zeros drive setpoints (`0x204` speed setpoint drops to 0, gear `N`).
   - SYS commands max brake (`0x7B9` stroke 1140).
3. Execute recovery:
   - Send `host:host_estop_reset_req` (`reset_token: 0x5253` with advancing counter).
   - Verify both nodes return cleanly to `STANDBY`.

---

## 6. Codebase Scripts Reference (`control-toolkit/backend/scripts`)

The test and diagnostic utilities are located directly inside the codebase:

- **1. Inspect Live CAN Bus Traffic & Safety Status:**
  ```powershell
  python control-toolkit/backend/scripts/check_can_bus.py
  # Continuous monitor mode:
  python control-toolkit/backend/scripts/check_can_bus.py --watch 1.0
  ```

- **2. Automated Hardware Bench Control Operations:**
  ```powershell
  # Check/Ensure Session & Bench TX active:
  python control-toolkit/backend/scripts/bench_control_ops.py --session

  # Execute ESTOP Reset:
  python control-toolkit/backend/scripts/bench_control_ops.py --reset

  # Command Power State:
  python control-toolkit/backend/scripts/bench_control_ops.py --power on
  python control-toolkit/backend/scripts/bench_control_ops.py --power off

  # Command Mode:
  python control-toolkit/backend/scripts/bench_control_ops.py --mode auto
  python control-toolkit/backend/scripts/bench_control_ops.py --mode manual

  # Stream Drive Setpoint (speed in mm/s) & Verify Actuator Outputs:
  python control-toolkit/backend/scripts/bench_control_ops.py --drive 1000 --duration 5.0

  # Run Full Clean Cycle (Reset -> Power ON -> Mode AUTO -> Drive 1000 mm/s):
  python control-toolkit/backend/scripts/bench_control_ops.py --full-cycle
  ```

- **3. Flash Firmwares:**
  ```powershell
  # Flash RT Controller (COM6):
  pio run -d rt-esp32 -e hardware_bench -t upload --upload-port COM6

  # Flash SYS Controller (COM10):
  pio run -d sys-esp32 -e hardware_bench -t upload --upload-port COM10
  ```

- **4. Backend Server:**
  ```powershell
  cd e:\work\etrike\control-toolkit\backend
  python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001 --log-level info
  ```

