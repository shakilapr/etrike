"""Freshness aging (workplan §1.5)."""

from __future__ import annotations

import time

from control_toolkit.models.frames import ChannelId
from control_toolkit.models.state import FreshnessState, MessageState
from control_toolkit.pipeline.freshness import classify
from control_toolkit.state.latest import LatestStore

MS = 1_000_000  # nanoseconds per millisecond


# ---- pure classifier (cycle 100ms -> Late >200ms, Missing >500ms) -----------


def test_unseen_when_never_observed():
    assert classify("ok", None, 100, 0) is FreshnessState.UNSEEN


def test_live_when_fresh_and_valid():
    assert classify("ok", 0, 100, 100 * MS) is FreshnessState.LIVE


def test_invalid_when_fresh_but_codec_failed():
    assert classify("checksum_mismatch", 0, 100, 100 * MS) is FreshnessState.INVALID


def test_late_between_thresholds():
    assert classify("ok", 0, 100, 300 * MS) is FreshnessState.LATE


def test_missing_past_threshold():
    assert classify("ok", 0, 100, 600 * MS) is FreshnessState.MISSING


def test_event_message_never_ages():
    # cycle 0 (event/aperiodic): Live when fresh (<300ms), Missing after long idle (>2s).
    assert classify("ok", 0, 0, 100 * MS) is FreshnessState.LIVE
    assert classify("ok", 0, 0, 3_000 * MS) is FreshnessState.MISSING


def test_staleness_dominates_invalidity():
    # An invalid message that stops arriving becomes Missing, not stuck Invalid.
    assert classify("checksum_mismatch", 0, 100, 600 * MS) is FreshnessState.MISSING


def test_thresholds_have_floors_for_fast_messages():
    # 10ms cycle: floors apply (Late >150ms, Missing >500ms), not 20ms/50ms.
    assert classify("ok", 0, 10, 100 * MS) is FreshnessState.LIVE
    assert classify("ok", 0, 10, 200 * MS) is FreshnessState.LATE
    assert classify("ok", 0, 10, 600 * MS) is FreshnessState.MISSING


# ---- store re-aging ----------------------------------------------------------


def test_store_reclassify_ages_in_place():
    store = LatestStore()
    store.upsert(
        MessageState(
            bus="low",
            can_id=0x7FE,
            key="sys:sys_heartbeat",
            name="SYS_HEARTBEAT",
            last_seen_ns=0,
            expected_rate_hz=10.0,  # 100ms cycle
            freshness=FreshnessState.LIVE,
            validation_status="ok",
        )
    )
    store.reclassify_freshness(now_ns=300 * MS)
    assert store.snapshot(now_ns=300 * MS).messages[0].freshness is FreshnessState.LATE
    store.reclassify_freshness(now_ns=600 * MS)
    assert store.snapshot(now_ns=600 * MS).messages[0].freshness is FreshnessState.MISSING


# ---- real-time API integration ----------------------------------------------


def test_freshness_ages_to_missing_via_api(client):
    life = client.app.state.lifecycle
    if getattr(life, "sys_sil", None):
        life.sys_sil.stop()

    transport = life.transport
    transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))

    # Wait until Live.
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        msgs = {m["name"]: m for m in client.get("/api/v1/state").json()["messages"]}
        if msgs.get("SYS_HEARTBEAT", {}).get("freshness") == "live":
            break
        time.sleep(0.02)
    assert msgs["SYS_HEARTBEAT"]["freshness"] == "live"

    # Stop injecting; past the 500ms Missing threshold it must age out.
    time.sleep(0.8)
    msgs = {m["name"]: m for m in client.get("/api/v1/state").json()["messages"]}
    assert msgs["SYS_HEARTBEAT"]["freshness"] == "missing"
