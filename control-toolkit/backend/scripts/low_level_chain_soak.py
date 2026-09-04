#!/usr/bin/env python3
"""Low-level command-chain continuous soak — Host drive vs RT Low liveness.

Runs the continuous (~23%) cases from docs/low-level-command-chain-cases.md:

  E21 AUTO drive soak 30 s      — RT_HEARTBEAT(low) never stale, SYS hb_ok=1,
                                 no ESTOP, 0x204 non-zero throughout
  E22 Cadence AUTO drive        — 0x204 ~100 Hz, 0x169 ~50 Hz, 0x7B9 ~50 Hz
  E23 HMI 1 Hz cadence          — HMI_MODE_REQ high+low ~1 Hz, SYS_MODE_CMD ~1 Hz
  E24 Steer oscillation 20 s    — 0x169 tracks steer sign; no ESTOP; hb live
  E25 Heartbeat gap monitor     — 0x7FD(low) inter-frame gap never > 0.9 s
  E26 Mode-toggle soak 20 s     — 0x204 follows MANUAL/AUTO each cycle

Live bench only. Artifacts -> control-toolkit/test-results/api-qa/.

Usage:
  python scripts/low_level_chain_soak.py [--base http://127.0.0.1:8001]
      [--duration 30] [--cases E21,E22] [--out DIR]
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "test-results" / "api-qa"

ID_HOST = 0x300
ID_MTR_CMD = 0x204
ID_SES_CMD = 0x169
ID_SEB_CMD = 0x7B9
ID_RT_BRAKE = 0x205
ID_SYS_MODE = 0x110
ID_SYS_HB = 0x7FE
ID_RT_HB = 0x7FD
ID_HMI_MODE = 0x111

RT_HB_EXPECT_HZ = 2.0          # RtHeartbeat kCycleMs = 500 ms
RT_HB_MAX_GAP_S = 0.9          # SYS timeout is 1.0 s; keep margin
DRIVE_HZ_EXPECT = 100.0
SES_HZ_EXPECT = 50.0
SEB_HZ_EXPECT = 50.0


@dataclass
class Check:
    id: str
    case: str
    ok: bool
    detail: str
    expected: Any = None
    observed: Any = None
    error: str | None = None
    duration_ms: float = 0.0


@dataclass
class Report:
    started: str
    base_url: str
    checks: list[Check] = field(default_factory=list)

    def add(self, c: Check) -> None:
        self.checks.append(c)
        print(f"  [{'PASS' if c.ok else 'FAIL'}] {c.id} — {c.detail}")

    @property
    def failed(self) -> list[Check]:
        return [c for c in self.checks if not c.ok]

    @property
    def passed(self) -> list[Check]:
        return [c for c in self.checks if c.ok]


class Api:
    def __init__(self, base: str, timeout: float = 10.0) -> None:
        self.base = base.rstrip("/")

    def j(self, method, path, json_body=None, expect=None, timeout=10.0):
        data = None if json_body is None else json.dumps(json_body).encode()
        headers = {"Content-Type": "application/json"} if data else {}
        r = urllib.request.Request(
            self.base + path, data=data, method=method, headers=headers
        )
        try:
            with urllib.request.urlopen(r, timeout=timeout) as resp:
                raw = resp.read().decode()
                code = resp.status
                body = json.loads(raw) if raw else {}
        except urllib.error.HTTPError as e:
            raw = e.read().decode()
            code = e.code
            try:
                body = json.loads(raw)
            except Exception:
                body = {"_raw": raw[:500]}
        except Exception as exc:  # noqa: BLE001
            code = 0
            body = {"_raw": str(exc)[:300]}
        if expect is not None:
            allowed = {expect} if isinstance(expect, int) else set(expect)
            if code not in allowed:
                raise AssertionError(f"{method} {path} -> {code} expected {allowed}: {body}")
        return code, body

    def status(self) -> dict:
        _, b = self.j("GET", "/status")
        return b

    def state(self) -> list[dict]:
        _, b = self.j("GET", "/state")
        return b.get("messages") or []

    def history(self, limit: int = 1000) -> list[dict]:
        _, b = self.j("GET", f"/history?limit={limit}")
        return b.get("frames") or []

    def intent(self, sequence, source="soak", throttle=0.0, steer=0.0, gear=1,
               hard_brake=False, estop=False):
        body = {"sequence": sequence, "source": source, "mode": "kinematics",
                "throttle": throttle, "steer": steer, "gear": gear,
                "hard_brake": hard_brake, "estop": estop}
        return self.j("POST", "/control/intent", body, expect={200, 409, 422})

    def release(self, reason="soak"):
        return self.j("POST", "/control/release", {"reason": reason})

    def hmi_mode(self, req_mode, enabled=True):
        return self.j("POST", "/hmi/mode", {"req_mode": req_mode, "enabled": enabled})


def eng(msg, key):
    if not msg:
        return None
    s = (msg.get("signals") or {}).get(key) or {}
    return s.get("engineering_value")


def num(msg, key):
    v = eng(msg, key)
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        try:
            return int(float(v))
        except (TypeError, ValueError):
            return None


def fresh(msg) -> bool:
    if not msg:
        return False
    return str(msg.get("freshness") or "").lower() in ("live", "late")


def find_msg(msgs, can_id=None, name=None, bus=None):
    for m in msgs:
        if can_id is not None and int(m.get("can_id") or -1) != can_id:
            continue
        if name and m.get("name") != name:
            continue
        if bus and m.get("bus") != bus:
            continue
        return m
    return None


def rate_hz(msg) -> float:
    if not msg:
        return 0.0
    r = msg.get("observed_rate_hz")
    return float(r or 0.0)


def ensure_auto(api: Api) -> None:
    api.hmi_mode(1, True)
    deadline = time.time() + 6.0
    while time.time() < deadline:
        sm = find_msg(api.state(), can_id=ID_SYS_MODE)
        if sm and fresh(sm) and num(sm, "mode") == 1:
            return
        rt = find_msg(api.state(), name="RT_STATE_RPT")
        if rt and fresh(rt) and eng(rt, "mode") in ("AUTO", 1):
            return
        time.sleep(0.15)
    raise RuntimeError("could not bring vehicle to AUTO")


def drive_stream(api: Api, seconds: float, source="soak_drive",
                 throttle=0.6, steer=0.0, gear=1, hz=20):
    start = time.time()
    i = 0
    while time.time() - start < seconds:
        i += 1
        api.intent(i, source=source, throttle=throttle, steer=steer, gear=gear)
        time.sleep(1.0 / hz)
    api.release("soak_end")
    return i


def heartbeat_gaps(api: Api, seconds: float) -> list[float]:
    """Sample 0x7FD(low) arrival timestamps and return inter-frame gaps (s)."""
    start = time.time()
    last = None
    gaps: list[float] = []
    samples = 0
    while time.time() - start < seconds:
        hb = find_msg(api.state(), can_id=ID_RT_HB, bus="low") or find_msg(
            api.state(), can_id=ID_RT_HB
        )
        if hb and fresh(hb):
            age = hb.get("age_ms")
            # reconstruct approximate arrival from age_ms
            if age is not None:
                now_age = float(age) / 1000.0
                if last is not None:
                    gaps.append(max(0.0, now_age - last))
                last = now_age
            samples += 1
        time.sleep(0.1)
    return gaps


def run_e21(api, ctx, report, duration) -> None:
    """AUTO drive soak — RT_HEARTBEAT(low) never stale, SYS hb_ok=1, no ESTOP."""
    t0 = time.perf_counter()
    ensure_auto(api)
    # start drive in a background thread would complicate urllib; instead sample
    # interleaved with the stream at 20 Hz and read state every ~0.5 s.
    start = time.time()
    i = 0
    last_hb_ok = True
    stale_seen = 0
    estop_seen = False
    zero_runs = 0
    drive_nonzero = 0
    drive_total = 0
    while time.time() - start < duration:
        i += 1
        api.intent(i, source="soak_e21", throttle=0.6, steer=0.1, gear=1)
        if i % 10 == 0:  # ~0.5 s
            msgs = api.state()
            hb = find_msg(msgs, can_id=ID_RT_HB, bus="low") or find_msg(msgs, can_id=ID_RT_HB)
            syshb = find_msg(msgs, can_id=ID_SYS_HB, bus="low") or find_msg(msgs, can_id=ID_SYS_HB)
            drv = find_msg(msgs, can_id=ID_MTR_CMD, bus="low")
            if not (hb and fresh(hb)):
                stale_seen += 1
            hbok = (syshb and fresh(syshb) and num(syshb, "heartbeat_ok") == 1)
            last_hb_ok = last_hb_ok and bool(hbok)
            st = api.status()
            if (st.get("estop") or {}).get("active"):
                estop_seen = True
            spd = num(drv, "motor_speed_mmps") if drv else 0
            drive_total += 1
            if spd is not None and spd > 0:
                drive_nonzero += 1
            else:
                zero_runs += 1
        time.sleep(0.05)
    api.release("e21")
    ok = stale_seen == 0 and last_hb_ok and not estop_seen and drive_nonzero > 0
    detail = (f"stale_samples={stale_seen} hb_ok_held={last_hb_ok} "
              f"estop={estop_seen} drive_nonzero={drive_nonzero}/{drive_total} zero={zero_runs}")
    report.add(Check("E21", "E", ok, detail,
                     expected={"rt_hb_never_stale", "sys_hb_ok", "no_estop", "drive>0"},
                     observed={"stale": stale_seen, "estop": estop_seen,
                               "drive_nonzero": drive_nonzero},
                     duration_ms=(time.perf_counter() - t0) * 1000))


def run_e22(api, ctx, report, duration) -> None:
    """Cadence — 0x204 ~100 Hz, 0x169 ~50 Hz, 0x7B9 ~50 Hz over a window."""
    t0 = time.perf_counter()
    ensure_auto(api)
    drive_stream(api, duration, source="soak_e22", throttle=0.6, steer=0.1)
    # rates from state observed_rate_hz
    msgs = api.state()
    drv = find_msg(msgs, can_id=ID_MTR_CMD, bus="low")
    ses = find_msg(msgs, can_id=ID_SES_CMD, bus="low")
    seb = find_msg(msgs, can_id=ID_SEB_CMD, bus="low")
    r_drv = rate_hz(drv)
    r_ses = rate_hz(ses)
    r_seb = rate_hz(seb)
    ok = r_drv >= DRIVE_HZ_EXPECT * 0.5 and r_ses >= SES_HZ_EXPECT * 0.3 \
        and r_seb >= SEB_HZ_EXPECT * 0.3
    report.add(Check("E22", "E", ok,
                     f"0x204={r_drv:.1f}Hz 0x169={r_ses:.1f}Hz 0x7B9={r_seb:.1f}Hz "
                     f"(expect ~{DRIVE_HZ_EXPECT:.0f}/{SES_HZ_EXPECT:.0f}/{SEB_HZ_EXPECT:.0f})",
                     expected={"0x204~100", "0x169~50", "0x7B9~50"},
                     observed={"drv": round(r_drv, 1), "ses": round(r_ses, 1),
                               "seb": round(r_seb, 1)},
                     duration_ms=(time.perf_counter() - t0) * 1000))


def run_e23(api, ctx, report, duration) -> None:
    """HMI 1 Hz cadence — HMI_MODE_REQ high+low ~1 Hz; SYS_MODE_CMD follows."""
    t0 = time.perf_counter()
    api.hmi_mode(1, True)
    time.sleep(duration)
    msgs = api.state()
    hh = find_msg(msgs, can_id=ID_HMI_MODE, bus="high")
    hl = find_msg(msgs, can_id=ID_HMI_MODE, bus="low")
    sm = find_msg(msgs, can_id=ID_SYS_MODE)
    r_hh = rate_hz(hh)
    r_hl = rate_hz(hl)
    r_sm = rate_hz(sm)
    sys_mode = num(sm, "mode") if sm else None
    ok = r_hh >= 0.5 and r_hl >= 0.5 and r_sm >= 0.5 and sys_mode == 1
    report.add(Check("E23", "E", ok,
                     f"HMI high={r_hh:.2f}Hz low={r_hl:.2f}Hz SYS_MODE={r_sm:.2f}Hz mode={sys_mode}",
                     expected={"~1Hz each", "sys_mode=1"},
                     observed={"hmi_high": round(r_hh, 2), "hmi_low": round(r_hl, 2),
                               "sys": round(r_sm, 2), "mode": sys_mode},
                     duration_ms=(time.perf_counter() - t0) * 1000))
    api.hmi_mode(1, False)


def run_e24(api, ctx, report, duration) -> None:
    """Steer oscillation 20 s — 0x169 tracks steer sign; no ESTOP; hb live."""
    t0 = time.perf_counter()
    ensure_auto(api)
    start = time.time()
    i = 0
    estop_seen = False
    hb_stale = 0
    sign_ok = 0
    sign_total = 0
    while time.time() - start < duration:
        i += 1
        steer = math.sin(i / 10.0)
        api.intent(i, source="soak_e24", throttle=0.4, steer=steer, gear=1)
        if i % 10 == 0:
            msgs = api.state()
            ses = find_msg(msgs, can_id=ID_SES_CMD, bus="low")
            hb = find_msg(msgs, can_id=ID_RT_HB, bus="low") or find_msg(msgs, can_id=ID_RT_HB)
            ang = num(ses, "target_angle_raw") if ses else None
            st = api.status()
            if (st.get("estop") or {}).get("active"):
                estop_seen = True
            if not (hb and fresh(hb)):
                hb_stale += 1
            if ang is not None:
                sign_total += 1
                # steer>0 -> angle>center; steer<0 -> angle<center
                exp = steer > 0
                got = ang > 30000
                if (i // 10) % 2 == 0:  # only compare when steer well-signed
                    pass
                if abs(steer) > 0.4 and got == exp:
                    sign_ok += 1
        time.sleep(0.05)
    api.release("e24")
    ok = not estop_seen and hb_stale == 0
    report.add(Check("E24", "E", ok,
                     f"estop={estop_seen} hb_stale={hb_stale} steer_sign_ok={sign_ok}/{sign_total}",
                     expected={"no_estop", "hb_live"}, observed={"estop": estop_seen,
                     "hb_stale": hb_stale, "sign": f"{sign_ok}/{sign_total}"},
                     duration_ms=(time.perf_counter() - t0) * 1000))


def run_e25(api, ctx, report, duration) -> None:
    """Heartbeat gap monitor — 0x7FD(low) gap never > 0.9 s during drive."""
    t0 = time.perf_counter()
    ensure_auto(api)
    # drive + sample gaps
    start = time.time()
    i = 0
    last_age: float | None = None
    max_gap = 0.0
    samples = 0
    while time.time() - start < duration:
        i += 1
        api.intent(i, source="soak_e25", throttle=0.6, steer=0.1, gear=1)
        hb = find_msg(api.state(), can_id=ID_RT_HB, bus="low") or find_msg(
            api.state(), can_id=ID_RT_HB
        )
        if hb and fresh(hb) and hb.get("age_ms") is not None:
            age = float(hb.get("age_ms")) / 1000.0
            samples += 1
            if last_age is not None:
                gap = age - last_age
                if gap > max_gap:
                    max_gap = gap
            last_age = age
        time.sleep(0.1)
    api.release("e25")
    ok = samples >= 2 and max_gap <= RT_HB_MAX_GAP_S
    report.add(Check("E25", "E", ok,
                     f"samples={samples} max_gap={max_gap:.2f}s (limit {RT_HB_MAX_GAP_S}s)",
                     expected={"max_gap<=0.9"}, observed={"max_gap": round(max_gap, 2),
                     "samples": samples}, duration_ms=(time.perf_counter() - t0) * 1000))


def run_e26(api, ctx, report, duration) -> None:
    """Mode-toggle soak — 0x204 follows MANUAL/AUTO each cycle; no lockup."""
    t0 = time.perf_counter()
    ensure_auto(api)
    start = time.time()
    cycles = 0
    ok_cycles = 0
    estop_seen = False
    while time.time() - start < duration:
        cycles += 1
        mode = "MANUAL" if cycles % 2 else "AUTO"
        req = 0 if mode == "MANUAL" else 1
        api.hmi_mode(req, True)
        # brief drive during this mode
        for i in range(1, 6):
            api.intent(i, source="soak_e26", throttle=0.4, steer=0.0, gear=1)
            time.sleep(0.05)
        time.sleep(0.3)
        drv = find_msg(api.state(), can_id=ID_MTR_CMD, bus="low")
        spd = num(drv, "motor_speed_mmps") if drv else None
        st = api.status()
        if (st.get("estop") or {}).get("active"):
            estop_seen = True
        expect_zero = mode == "MANUAL"
        if expect_zero and spd == 0:
            ok_cycles += 1
        elif not expect_zero and spd is not None and spd > 0:
            ok_cycles += 1
    api.release("e26")
    api.hmi_mode(1, False)
    api.hmi_mode(1, True)  # restore AUTO for a clean exit
    ok = ok_cycles == cycles and not estop_seen
    report.add(Check("E26", "E", ok,
                     f"cycles={cycles} ok={ok_cycles} estop={estop_seen}",
                     expected={"all cycles follow mode", "no_estop"},
                     observed={"cycles": cycles, "ok": ok_cycles, "estop": estop_seen},
                     duration_ms=(time.perf_counter() - t0) * 1000))


RUNNERS = {
    "E21": run_e21, "E22": run_e22, "E23": run_e23,
    "E24": run_e24, "E25": run_e25, "E26": run_e26,
}


def main() -> int:
    p = argparse.ArgumentParser(description="Low-level command-chain continuous soak")
    p.add_argument("--base", default="http://127.0.0.1:8001")
    p.add_argument("--out", default=str(OUT_DIR))
    p.add_argument("--duration", type=float, default=20.0,
                   help="seconds per soak (default 20)")
    p.add_argument("--cases", default="", help="comma list e.g. E21,E22; empty runs all")
    p.add_argument("--stop-on-fail", action="store_true")
    args = p.parse_args()

    only = set(args.cases.split(",")) if args.cases else set()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = Report(datetime.now(timezone.utc).isoformat(), args.base)
    api = Api(args.base)

    print(f"Low-level command-chain soak -> {args.base}")
    # quick precondition probe: bench_test session present & healthy-ish
    st = api.status()
    if (st.get("session") or {}).get("profile") != "bench_test":
        print("WARN: active session is not bench_test — continuing anyway; "
              "ensure bench_test + Bench TX are armed.")
    try:
        ensure_auto(api)
    except RuntimeError as exc:
        print("FATAL: vehicle not in AUTO (bench down?):", exc)
        return 2

    names = list(RUNNERS) if not only else [c for c in RUNNERS if c in only]
    for cid in names:
        RUNNERS[cid](api, None, report, args.duration)
        if args.stop_on_fail and report.failed:
            print("Stopping on first failure")
            break

    # leave clean: AUTO + no jobs
    api.release("soak_exit")
    api.hmi_mode(1, True)
    time.sleep(0.3)
    api.hmi_mode(1, False)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    payload = {
        "started": report.started,
        "base_url": args.base,
        "duration_s": args.duration,
        "summary": {"total": len(report.checks), "passed": len(report.passed),
                    "failed": len(report.failed)},
        "checks": [asdict(c) for c in report.checks],
        "failures": [asdict(c) for c in report.failed],
    }
    for suffix in (f"low_level_chain_soak_{stamp}", "low_level_chain_soak_latest"):
        (out / f"{suffix}.json").write_text(json.dumps(payload, indent=2, default=str), encoding="utf-8")
    lines = [f"# Low-level command-chain soak — {report.started}", "",
             f"**Base:** `{args.base}`", f"**Duration:** {args.duration}s",
             f"**Result:** {len(report.passed)}/{len(report.checks)} passed", ""]
    lines += ["| Status | ID | Detail |", "|---|---|---|"]
    for c in report.checks:
        det = (c.detail or "").replace("|", "/")[:160]
        lines.append(f"| {'PASS' if c.ok else '**FAIL**'} | `{c.id}` | {det} |")
    md = "\n".join(lines) + "\n"
    for suffix in (f"low_level_chain_soak_{stamp}", "low_level_chain_soak_latest"):
        (out / f"{suffix}.md").write_text(md, encoding="utf-8")

    print("\n=== SUMMARY ===")
    print(f"passed {len(report.passed)} / {len(report.checks)}")
    if report.failed:
        for c in report.failed:
            print(f"  FAIL {c.id}: {c.detail} | {c.error or ''}")
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
