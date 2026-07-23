# System Run Modes & Developer Override

## Run Modes

Set via PlatformIO environment at compile time (`ETRIKE_SYSTEM_RUN_MODE`).

| Mode | Value | Env | Behavior |
|------|-------|-----|----------|
| **Production** | `0` | `vehicle` | All safety checks enforced. No bypasses. |
| **Hardware Bench** | `1` | `hardware_bench` | Checks enforced **unless** GPIO 42 is grounded at boot. |
| **Software Bench** | `2` | `bench` | All peer/sync checks bypassed unconditionally. |

## How to Build & Flash

```bash
# Software bench (bypasses everything, no jumper needed)
pio run -e bench -t upload --upload-port COM10   # RT
pio run -e bench -t upload --upload-port COM6    # SYS

# Hardware bench (bypass only if GPIO 42 grounded)
pio run -e hardware_bench -t upload --upload-port COM10
pio run -e hardware_bench -t upload --upload-port COM6

# Production (no bypasses)
pio run -e vehicle -t upload --upload-port COM10
pio run -e vehicle -t upload --upload-port COM6
```

## Developer Override Pin (Mode 1 only)

| Property | Value |
|----------|-------|
| GPIO | **42** |
| Active state | **LOW** (grounded) |
| Internal pull-up | Enabled by firmware |
| Strapping pin? | No — safe to use |

Connect GPIO 42 to GND at boot to activate `bench_solo_mode`.

## What `bench_solo_mode` Bypasses

| Check | Bypassed? |
|-------|-----------|
| RT heartbeat timeout (SYS side) | Yes |
| SYS heartbeat timeout (RT side) | Yes |
| Host heartbeat timeout (RT side) | Yes |
| Host command stale watchdog (RT side) | Yes |
| EPS (steering) sync check | Yes |
| SEB (brake) sync check | Yes |
| MTR absent check | Yes |
| **HW ESTOP button (GPIO 1)** | **No — never bypassable** |

## ESTOP Button (GPIO 1)

| Property | Value |
|----------|-------|
| GPIO | **1** |
| Type | NC (normally closed), active-low |
| Pull-down | External 10k |
| Behavior | **Grounded = ESTOP pressed. Unbypassable in any mode.** |

**Do NOT ground GPIO 1 to suppress ESTOP.** Leave it floating or connect to 3.3V through the NC contact. Grounding it = permanent ESTOP.

## Quick Reference

| Goal | Do this |
|------|---------|
| Bench test, no peers | Flash `bench` env, leave GPIO 1 floating |
| Bench test with HW jumper | Flash `hardware_bench`, ground GPIO 42, leave GPIO 1 floating |
| Production | Flash `vehicle`, wire ESTOP button NC to 3.3V |
