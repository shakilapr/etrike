"""H11 — latency benchmarks and High<->Low gateway stress.

Measures end-to-end latencies across the real buses using backend frame arrival
timestamps, plus gateway loss under load and survival of a High-rate burst.

Latency bounds are deliberately generous (they exist to catch gross regressions,
not to be tight SLAs): measured medians on the bench are roughly
  * High->Low relay         ~2-4 ms
  * Low->High relay         ~3-4 ms
  * 0x300 speed -> 0x204    ~10 ms
  * 0x301 brake -> 0x205    ~20 ms
  * 0x301 brake -> 0x7B9    ~40 ms
  * 0x300 yaw   -> 0x169    ~16 ms
"""

from __future__ import annotations

import time

import pytest

from harness import (
    CAN_RT_BRAKE_CMD,
    CAN_RT_DRIVE_CMD,
    CAN_SES_REQ,
    GEAR_D,
    HIGH,
    LOW,
    is_live,
    signal_of,
)
from metrics import (
    ID_HOST_BRAKE,
    ID_HOST_DRIVE,
    ID_HMI_PWR,
    ID_SEB_REQ,
    ID_SYS_NODE,
    ID_SYS_SAFETY,
    gateway_loss,
    measure_relay_h2l,
    measure_relay_l2h,
    measure_step,
    median_or_none,
)
from scenario import speed_is, track

pytestmark = pytest.mark.hw_bench

# Generous ceilings (ms).
RELAY_BOUND_MS = 40.0
COMMAND_BOUND_MS = 80.0
FAST_COMMAND_BOUND_MS = 60.0


def test_high_to_low_relay_latency(bench):
    """SYS/Host High frame reaches the Low bus via RT within the relay bound."""
    lats = measure_relay_h2l(bench, cid=ID_HMI_PWR, samples=8)
    med = median_or_none(lats)
    assert med is not None and med <= RELAY_BOUND_MS, (
        f"High->Low relay median {med} ms exceeds {RELAY_BOUND_MS} ms (samples={lats})"
    )


def test_low_to_high_relay_latency(bench):
    """SYS Low frames reach the High bus via RT within the relay bound."""
    safety = median_or_none(measure_relay_l2h(bench, ID_SYS_SAFETY, counter_byte=3))
    node = median_or_none(measure_relay_l2h(bench, ID_SYS_NODE, counter_byte=6, seconds=3.0))
    assert safety is not None and safety <= RELAY_BOUND_MS, (
        f"Low->High 0x011 relay median {safety} ms exceeds {RELAY_BOUND_MS} ms"
    )
    assert node is not None and node <= RELAY_BOUND_MS, (
        f"Low->High 0x500 relay median {node} ms exceeds {RELAY_BOUND_MS} ms"
    )


def test_command_to_unit_latency(auto_ready):
    """Host command -> low-level unit frame latency for speed, brake and steering."""
    bench = auto_ready

    # Speed: 0x300 -> 0x204 (MTR).
    bench.start_drive(0, gear=GEAR_D)
    speed_med = median_or_none(measure_step(
        bench,
        lambda v: bench.start_drive(v, gear=GEAR_D),
        (HIGH, ID_HOST_DRIVE, lambda r, v: int.from_bytes(r[0:4], "big", signed=True) == v),
        (LOW, CAN_RT_DRIVE_CMD, lambda r, v: int.from_bytes(r[0:4], "big", signed=True) == v),
        [900, 1000, 1100, 1200, 1300, 1400],
        reset=lambda: bench.start_drive(0, gear=GEAR_D),
    ))
    assert speed_med is not None and speed_med <= FAST_COMMAND_BOUND_MS, (
        f"0x300->0x204 median {speed_med} ms exceeds {FAST_COMMAND_BOUND_MS} ms"
    )

    # Brake: 0x301 -> 0x205 (SYS intent) and -> 0x7B9 (SEB).
    brake_med = median_or_none(measure_step(
        bench,
        lambda v: bench.send_brake(v),
        (HIGH, ID_HOST_BRAKE, lambda r, v: int.from_bytes(r[0:4], "big", signed=True) == v),
        (LOW, CAN_RT_BRAKE_CMD, lambda r, v: int.from_bytes(r[0:4], "big", signed=True) == v),
        [2000, 2100, 2200, 2300, 2400, 2500],
        reset=lambda: bench.send_brake(0),
    ))
    assert brake_med is not None and brake_med <= COMMAND_BOUND_MS, (
        f"0x301->0x205 median {brake_med} ms exceeds {COMMAND_BOUND_MS} ms"
    )

    seb_med = median_or_none(measure_step(
        bench,
        lambda v: bench.send_brake(v),
        (HIGH, ID_HOST_BRAKE, lambda r, v: int.from_bytes(r[0:4], "big", signed=True) == v),
        (LOW, ID_SEB_REQ, lambda r, v: r[3] == (v + 25) // 50),
        [2000, 2100, 2200, 2300, 2400, 2500],
        reset=lambda: bench.send_brake(0),
    ))
    assert seb_med is not None and seb_med <= COMMAND_BOUND_MS, (
        f"0x301->0x7B9 median {seb_med} ms exceeds {COMMAND_BOUND_MS} ms"
    )

    # Steering: 0x300 yaw -> 0x169 (SES).
    bench.start_drive(1200, yaw_rate_mrad_s=0, gear=GEAR_D)
    steer_med = median_or_none(measure_step(
        bench,
        lambda v: bench.start_drive(1200, yaw_rate_mrad_s=v, gear=GEAR_D),
        (HIGH, ID_HOST_DRIVE, lambda r, v: int.from_bytes(r[4:7], "big", signed=True) == v),
        (LOW, CAN_SES_REQ, lambda r, v: abs(((r[1] << 8) | r[2]) - 30000) > 200),
        [400, 460, 520, 580, 640, 700],
        reset=lambda: bench.start_drive(1200, yaw_rate_mrad_s=0, gear=GEAR_D),
    ))
    assert steer_med is not None and steer_med <= COMMAND_BOUND_MS, (
        f"0x300->0x169 median {steer_med} ms exceeds {COMMAND_BOUND_MS} ms"
    )


def test_gateway_loss_under_load(bench):
    """0x302 streamed High->Low at ~100 Hz loses nothing through the gateway."""
    tx, rx = gateway_loss(bench, seconds=2.5, period_ms=10.0)
    assert tx >= 40, f"High stream too slow to measure loss (tx={tx})"
    loss = 100.0 * (1 - rx / tx)
    assert loss <= 1.0, f"High->Low gateway loss {loss:.2f}% (tx={tx}, rx={rx})"


def test_high_rate_gateway_load_recovers(auto_ready):
    """Sustained ~200 Hz High->Low gateway load plus a High RX burst must not
    trip safety or leave the Low unit stream dead; drive re-establishes after."""
    bench = auto_ready
    bench.start_drive(800, gear=GEAR_D)
    track(bench, speed_is(800, tol=150), "load: baseline drive")

    # High->Low gateway load: forward 0x302 at ~200 Hz for ~2.5 s.
    ok, res = bench.inject(
        HIGH, "host:host_light_cmd",
        {"left_turn": 1, "right_turn": 0, "brake_light": 0, "headlight": 1},
        period_ms=5.0,
    )
    job = res.get("job_id") if ok else None
    # High RX load: unknown frames RT must receive and drop.
    for _ in range(100):
        bench.inject_raw(HIGH, 0x7FF, "0000000000000000")
    time.sleep(2.5)
    if job:
        bench.cancel_injection(job)
    bench.send_lights()

    # The Low unit stream must be alive and drive must re-establish.
    assert signal_of(bench.message(LOW, 0x011), "estop_active") == 0, "load latched ESTOP"
    state = {}
    engaged = False
    for _ in range(3):
        bench.start_drive(800, gear=GEAR_D)
        engaged, state = bench.wait_for(speed_is(800, tol=150), timeout_s=2.0)
        if engaged:
            break
    assert engaged, f"drive did not re-establish after gateway load: {state.get((LOW, CAN_RT_DRIVE_CMD))}"
    assert (bench.message(LOW, 0x204) or {}).get("observed_rate_hz", 0) >= 40, "0x204 rate collapsed"
    assert (bench.message(LOW, CAN_RT_DRIVE_CMD) or {}).get("freshness") == "live"
    assert is_live(bench.message(LOW, CAN_RT_DRIVE_CMD))
