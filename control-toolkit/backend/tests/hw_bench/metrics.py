"""Latency / gateway-loss measurement helpers for the H11 benchmark suite.

All timings use the backend's ``backend_arrival_ns`` per frame (the moment the
CANalyst delivered the frame), so they are end-to-end over the physical bus:

    High command (Host) -> RT -> Low unit frame (MTR/SES/SEB/SYS)
    SYS Low frame -> RT forward -> High

Nothing is synthesised; every number comes from real captured frames.
"""

from __future__ import annotations

import time
from statistics import median
from typing import Callable, Optional

from harness import HIGH, LOW, HwBench

# CAN ids used by the benchmarks.
ID_HMI_PWR = 0x112      # High->Low relay (has a rolling_counter)
ID_HOST_LIGHT = 0x302   # High->Low relay (bits)
ID_HOST_DRIVE = 0x300   # High command -> RT 0x204 / 0x169
ID_HOST_BRAKE = 0x301   # High command -> RT 0x205 / SYS 0x7B9
ID_RT_DRIVE = 0x204     # -> MTR
ID_RT_BRAKE = 0x205     # -> SYS
ID_SES_REQ = 0x169      # -> SES/SES
ID_SEB_REQ = 0x7B9      # -> SEB
ID_SYS_SAFETY = 0x011   # SYS -> Low, relayed to High
ID_SYS_NODE = 0x500     # SYS -> Low, relayed to High

SBW_ANGLE_OFFSET = 30000


def history_frames(bench: HwBench, limit: int = 4096) -> list[dict]:
    _, data = bench.request("GET", f"/history?limit={limit}")
    return (data or {}).get("frames", [])


def raw(frame: dict) -> bytes:
    return bytes.fromhex(frame["data_hex"])


def _delta_ms(a_ns: int, b_ns: int) -> float:
    return (a_ns - b_ns) / 1e6


def first_after(frames, bus, cid, pred, after_ns: int = 0) -> Optional[dict]:
    for f in frames:
        if (f["bus"] == bus and f["can_id"] == cid
                and f["backend_arrival_ns"] > after_ns and pred(raw(f))):
            return f
    return None


def median_or_none(values) -> Optional[float]:
    clean = [v for v in values if v is not None and v >= 0]
    return median(clean) if clean else None


def measure_step(
    bench: HwBench,
    apply: Callable[[int], None],
    src: tuple[str, int, Callable],
    tgt: tuple[str, int, Callable],
    values,
    reset: Optional[Callable[[], None]] = None,
) -> list[float]:
    """Measure command->unit latency: first target frame after the source frame.

    ``src``/``tgt`` are ``(bus, can_id, predicate(raw, value))``. Distinct
    ``values`` per sample avoid matching a stale frame.
    """
    lats: list[Optional[float]] = []
    for value in values:
        if reset:
            reset()
        time.sleep(0.35)
        apply(value)
        time.sleep(0.3)
        frames = history_frames(bench)
        sf = first_after(frames, src[0], src[1], lambda r, v=value: src[2](r, v))
        if not sf:
            lats.append(None)
            continue
        tf = first_after(frames, tgt[0], tgt[1], lambda r, v=value: tgt[2](r, v),
                         after_ns=sf["backend_arrival_ns"])
        lats.append(None if not tf else _delta_ms(tf["backend_arrival_ns"], sf["backend_arrival_ns"]))
    return [v for v in lats if v is not None]


def measure_relay_h2l(bench: HwBench, cid: int = ID_HMI_PWR, samples: int = 8) -> list[float]:
    """High->Low relay latency using the 0x112 rolling_counter for matching."""
    lats: list[float] = []
    for _ in range(samples):
        time.sleep(0.12)
        bench.inject(HIGH, "hmi:hmi_pwr_req", {"req_start": 1})
        time.sleep(0.2)
        frames = history_frames(bench)
        low = [f for f in frames if f["bus"] == LOW and f["can_id"] == cid]
        high = [f for f in frames if f["bus"] == HIGH and f["can_id"] == cid]
        for h in high[-4:]:
            ctr = raw(h)[1]
            match = [x for x in low if raw(x)[1] == ctr
                     and x["backend_arrival_ns"] >= h["backend_arrival_ns"]]
            if match:
                lats.append(_delta_ms(match[0]["backend_arrival_ns"], h["backend_arrival_ns"]))
    return lats


def measure_relay_l2h(bench: HwBench, cid: int, counter_byte: int,
                      seconds: float = 2.5) -> list[float]:
    """SYS Low frame -> RT relayed High frame latency, matched by counter."""
    _, data = bench.request("GET", "/history?limit=4096")
    base = max((f["global_sequence"] for f in (data or {}).get("frames", [])), default=0)
    time.sleep(seconds)
    frames = [f for f in history_frames(bench) if f["global_sequence"] > base]
    low = {raw(f)[counter_byte]: f["backend_arrival_ns"]
           for f in frames if f["bus"] == LOW and f["can_id"] == cid}
    high = {raw(f)[counter_byte]: f["backend_arrival_ns"]
            for f in frames if f["bus"] == HIGH and f["can_id"] == cid}
    return [high[c] / 1e6 - low[c] / 1e6 for c in low if c in high and high[c] >= low[c]]


def gateway_loss(bench: HwBench, seconds: float = 2.5, period_ms: float = 10.0) -> tuple[int, int]:
    """Stream 0x302 High->Low at ``period_ms``; return (tx, rx) frame counts."""
    _, data = bench.request("GET", "/history?limit=4096")
    base = max((f["global_sequence"] for f in (data or {}).get("frames", [])), default=0)
    ok, res = bench.inject(
        HIGH, "host:host_light_cmd",
        {"left_turn": 1, "right_turn": 0, "brake_light": 0, "headlight": 1},
        period_ms=period_ms,
    )
    job = res.get("job_id") if ok else None
    time.sleep(seconds)
    if job:
        bench.cancel_injection(job)
    time.sleep(0.2)
    frames = [f for f in history_frames(bench) if f["global_sequence"] > base]
    tx = sum(1 for f in frames if f["bus"] == HIGH and f["can_id"] == ID_HOST_LIGHT)
    rx = sum(1 for f in frames if f["bus"] == LOW and f["can_id"] == ID_HOST_LIGHT)
    return tx, rx
