# Phase 7 — Interactive control (keyboard kinematics)

**Status:** Complete for kinematics path (software / virtual)  
**Depends on:** Phase 5 TX pipeline  
**Firmware alignment:** verified against `host.yaml`, `shared_config.h`, MTR gear enum, SYS mode manager

## Firmware cross-check (fixes applied)

| Topic | Firmware / YAML | Toolkit correction |
|-------|-----------------|-------------------|
| Gear enum | 0=N 1=D 2=S 3=R (`host.yaml`, MTR) | UI preview used wrong P/R/D/S → **N/D/S/R** |
| Host speed limits | ±3000 / −500 mm/s (`shared_config.h`) | Control shaping uses same limits |
| Host stale | 500 ms (`kHostCmdStaleTimeoutMs`) | Intent watchdog ends TX after 500 ms silence |
| HOST_DRIVE period | 10 ms cycle (`host.yaml`) | Keyboard teleop schedules 10 ms re-encode |
| HMI mode | MANUAL/AUTO only (SYS rejects >1) | Protocol max=1; PURE_SIM UI-only label |
| ESTOP | DLC=0 `0x001` high **and** low | Dual-bus inject from header + `/control/intent` |

## Delivered

| Component | Path |
|-----------|------|
| Intent + shaping + stale watchdog | `services/control_intent.py` |
| API | `POST /control/intent`, `POST /control/release`, `GET /control/status` |
| Scheduler hot value update | `scheduler.update_values` |
| Control UI keyboard panel | `App.tsx` — WASD, Shift brake, Space ESTOP, blur/tab release |
| Preview gear firmware map | `VehiclePreview.tsx` |
| Tests | `test_firmware_alignment.py`, `test_keyboard_input.py`, `test_kinematics.py` |

## Direct actuators (added)

| Channel | CAN | Key |
|---------|-----|-----|
| Motor | Low 0x204 | `rt:rt_drive_cmd` |
| Steering | Low 0x169 | `ses:vcu_ses_req` |
| Brake | Low 0x7B9 | `seb:vcu_seb_req` |

`POST /api/v1/control/direct` — exclusive with kinematics intent. Control workspace cards start/stop streams.

## Deferred

- Gamepad HID mapping
- Full SES/SEB live feedback pairing polish
