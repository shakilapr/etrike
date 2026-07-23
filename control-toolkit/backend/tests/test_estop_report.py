"""ESTOP cause report from latest messages + host latch."""

from __future__ import annotations

from control_toolkit.models.state import FreshnessState, MessageState, SignalValue
from control_toolkit.services.estop_report import build_estop_report


def _msg(
    name: str,
    bus: str,
    can_id: int,
    signals: dict,
    *,
    freshness: FreshnessState = FreshnessState.LIVE,
    age_ms: float = 10.0,
) -> MessageState:
    return MessageState(
        bus=bus,
        can_id=can_id,
        name=name,
        freshness=freshness,
        age_ms=age_ms,
        signals={
            k: SignalValue(engineering_value=v, valid=True) for k, v in signals.items()
        },
    )


def test_clear_when_nothing_active():
    r = build_estop_report([], host_latch=False)
    assert r["active"] is False
    assert r["causes"] == []
    assert "clear" in r["summary"].lower()


def test_host_latch_only():
    r = build_estop_report([], host_latch=True)
    assert r["active"] is True
    assert r["host_latch"] is True
    assert any("Host inject" in c for c in r["causes"])
    assert r["primary_cause"] == "Host/toolkit SAFETY_ESTOP injection latch"


def test_rt_estop_reason_following_error():
    msgs = [
        _msg(
            "RT_STATE_RPT",
            "high",
            0x210,
            {"mode": 2, "safety_state": 1, "estop_reason": 3},
        )
    ]
    r = build_estop_report(msgs, host_latch=False)
    assert r["active"] is True
    assert r["rt"]["estop_reason"] == 3
    assert r["rt"]["estop_reason_label"] == "following_error"
    assert r["rt"]["estop_reason_display"] == "Steering following error"
    assert any("Steering following error" in c for c in r["causes"])
    assert "Steering following error" in r["summary"]
    assert r["primary_cause"] == "RT: Steering following error"
    assert r["cause_resolution"] == "reported"


def test_sys_estop_and_bus_001():
    msgs = [
        _msg("SYS_SAFETY_STS", "low", 0x11, {"estop_active": 1, "heartbeat_ok": 1}),
        _msg(
            "SAFETY_ESTOP",
            "high",
            0x001,
            {},
            freshness=FreshnessState.MISSING,
            age_ms=100.0,
        ),
    ]
    r = build_estop_report(msgs, host_latch=False)
    assert r["active"] is True
    assert r["sys"]["estop_active"] is True
    assert r["bus"]["high_0x001"] is True


def test_can_estop_reason_is_truthful_about_unknown_sender():
    msgs = [
        _msg(
            "RT_STATE_RPT",
            "high",
            0x210,
            {"mode": 2, "safety_state": 1, "estop_reason": 5},
        )
    ]
    r = build_estop_report(msgs, host_latch=False)
    assert r["cause_resolution"] == "unknown_origin"
    assert "originating node is not encoded" in r["primary_cause"]

    correlated = build_estop_report(msgs, host_latch=True)
    assert correlated["cause_resolution"] == "correlated"
    assert correlated["primary_cause"].startswith("Host/toolkit")
