# Hardware Bench Test Suite Guide (RT-ESP32 & SYS-ESP32)

Pytest suite for the combined **RT-ESP32** and **SYS-ESP32** controllers on the
hardware bench, with MTR, SES and SEB **physically absent**.

Supersedes the old script-based suites (`scripts/hardware_bench_suite.py`
suites, `system-test/`). See `control-toolkit/backend/tests/hw_bench/`.

---

## 1. Directives (do not deviate)

1. **No actuator mocking.** Never inject synthetic MTR / SES / SEB feedback
   (`0x206`, `0x201`, `0x721`, `0x120`). The rig runs `hardware_bench`
   (`ETRIKE_SYSTEM_RUN_MODE=1`, developer bypass) so missing peer feedback must
   not trip a safety stop. We only *observe* the output commands RT/SYS produce
   for those actuators and *inject* legitimate Host/HMI inputs.
2. **The backend owns the CANalyst-II.** Only the control-toolkit backend
   (uvicorn on `:8001`) may open the USB adapter. The suite talks REST only;
   opening `can.Bus(interface='canalystii')` elsewhere fails with `Errno 13`.
3. Both nodes are flashed with the **`hardware_bench`** PlatformIO environment.

---

## 2. Topology

```
                 HIGH CAN 500k (CANalyst-II CH0)          LOW CAN 500k (CH1)
  Host PC  <------------------------------>  rt-esp32  <------------------>  sys-esp32
  (:8001)                                    MCP2515                         TWAI
                                             TWAI (low)
```

* **High bus:** RT telemetry (`0x121`, `0x210`, `0x501`, `0x7FD`, `0x620`),
  forwarded SYS reports (`0x011`, `0x500`, `0x600`), Host commands
  (`0x300`, `0x301`, `0x302`, `0x320`/`0x400`, `0x114`, `0x001`).
* **Low bus:** SYS broadcasts (`0x7FE`, `0x110`, `0x113`, `0x011`, `0x500`,
  `0x600`, `0x7B9`), RT outputs (`0x204`, `0x205`, `0x169`, `0x210`, `0x501`,
  `0x7FD`).

---

## 3. Running the suite

```powershell
# terminal 1 — backend owns the adapter
cd control-toolkit/backend
python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001

# terminal 2
cd control-toolkit/backend
pytest tests/hw_bench -v                 # 28 tests
pytest tests/hw_bench -v --api-url http://127.0.0.1:8001
```

The session fixture skips everything cleanly when the backend is unreachable or
no physical link is connected, so the suite is CI-safe.

---

## 4. What is verified

| Module | Coverage |
|---|---|
| `test_h1_baseline.py` | link liveness, rates, node status, RT Low->High gateway |
| `test_h2_mode_power.py` | `0x113` power gateway, `0x110`/RT `0x210` AUTO+MANUAL, `output_enabled` |
| `test_h3_drive.py` | `0x300`->`0x204` forward/reverse/neutral, bench host-watchdog fail-safe |
| `test_h4_actuator_outputs.py` | `0x205` intent, `0x169` steer request, `0x7B9` released + pressure, bypass-without-peers |
| `test_h5_safety.py` | `0x001` ESTOP trip on High/Low, safe outputs, staged reset recovery |
| `test_h6_dynamic.py` | ramp, turn lights, trail braking, obstacle deceleration |
| `test_guard_no_actuator_mocks.py` | static guard: no actuator-mock keys, no direct CAN adapter access |

---

## 5. Verified firmware behavior (bench)

Observed and asserted by the suite after the bench fixes:

* **Rates.** `0x204` ~100 Hz, `0x205` ~50 Hz, `0x169` ~50 Hz, `0x501` ~50 Hz,
  `0x210` 10 Hz, `0x7B9` 50 Hz. (RT alternates `0x205`/`0x169`/status on a
  10 ms cadence; the TWAI driver allows `send()` a short bounded wait for a
  free TX slot.)
* **`0x7B9` follows brake intent.** With the bench brake bypass, SYS assumes an
  aligned SEB when none is present, so `BrakeControl` runs ACTIVE and maps the
  `0x205` kPa intent to a pressure request (`raw = round(kPa/50)`, e.g.
  3000 kPa -> 60). ESTOP still forces Stroke/max (raw 1140).
* **Bench host watchdog.** Losing the `0x300` stream zeroes `0x204` (fail-safe)
  without latching ESTOP; a resumed stream re-arms authority.
* **ESTOP reset is staged.** `hmi:host_estop_reset_req` (`reset_token 0x5253`)
  must be sent as **>= 2 counter-advancing frames** — the first only establishes
  the SYS `StreamValidity` baseline. On acceptance SYS exits ESTOP to MANUAL and
  emits two advancing `0x011 estop_active=0` frames, which RT requires to drop
  its own latch.

---

## 6. Bench runbook notes

* **SYS boots into ESTOP** after a flash/reboot and needs one staged reset
  before it accepts power/AUTO. The `bench` and `auto_ready` fixtures normalise
  this automatically.
* **MTR-absent reset bypass.** On the bench the remote reset is accepted even
  though no MTR ACKs the ESTOP (`g_bypass_mtr_absent` is honoured in
  `get_estop_reset_blockers`). On the vehicle, the physical ESTOP blocker and
  token checks remain enforced.
* If COM6/COM10 disappear from Windows, re-plug the USB-UART cables (they are
  CH343 bridges with no auto-reset wiring); then re-run. Use
  `python scripts/check_can_bus.py --watch 1` for a live sanity view.
