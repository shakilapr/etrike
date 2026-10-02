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
    assert "unconfirmed" in r["summary"].lower()

    # With normal active telemetry
    msgs = [_msg("SYS_HEARTBEAT", "low", 0x7FC, {"heartbeat_ok": 1, "can_ok": 1, "estop_active": 0})]
    r_live = build_estop_report(msgs, host_latch=False)
    assert r_live["active"] is False
    assert "clear" in r_live["summary"].lower()


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
        _msg("SYS_SAFETY_STS", "low", 0x11,
             {"estop_source": 1, "node_presence": 0x3F, "estop_reason": 1}),
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


def test_stale_ecu_fault_bits_do_not_remain_active():
    msgs = [
        _msg(
            "SYS_SAFETY_STS",
            "low",
            0x11,
            {"estop_active": 1, "heartbeat_ok": 0},
            freshness=FreshnessState.MISSING,
            age_ms=30_000.0,
        ),
        _msg(
            "SYS_DIAG_RPT",
            "low",
            0x600,
            {"estop_active": 1, "brake_fault": 1},
            freshness=FreshnessState.MISSING,
            age_ms=30_000.0,
        ),
        _msg(
            "RT_STATE_RPT",
            "high",
            0x210,
            {"mode": 2, "safety_state": 2, "estop_reason": 6},
            freshness=FreshnessState.MISSING,
            age_ms=30_000.0,
        ),
    ]

    r = build_estop_report(msgs, host_latch=False)

    assert r["active"] is False
    assert r["sys"]["estop_active"] is False
    assert r["sys"]["brake_fault"] is False
    assert r["rt"]["mode_estop"] is False
    assert r["rt"]["estop_reason"] == 0


def test_sys_node_status_latched_counts_as_active_source():
    msgs = [
        # 0x500 no longer carries the latch: the SYS latch is resolved from the
        # companion 0x011 SYS_SAFETY_STS.estop_source stream.
        _msg("SYS_SAFETY_STS", "low", 0x11,
             {"estop_source": 1, "node_presence": 0x3F, "estop_reason": 1}),
        _msg(
            "SYS_NODE_STATUS",
            "low",
            0x500,
            {
                "command_received": 1,
                "command_nonzero": 0,
                "command_executing": 0,
                "command_rejected": 0,
                "system_ready": 0,
                "degraded": 1,
                "block_mask_low": 0,
                "block_mask_high": 0,
            },
        )
    ]
    r = build_estop_report(msgs, host_latch=False)
    assert r["active"] is True
    assert r["nodes"]["sys"]["estop_latched"] is True
    assert any(s["id"] == "sys_node_latched" for s in r["sources"])
    assert "SYS NODE_STATUS latched" in r["summary"]
    # With the companion 0x011 present, the ECU ESTOP cause may rank first;
    # the node latch must still be reported as an active source either way.
    assert r["primary_cause"].startswith(
        ("Latched ESTOP in NODE_STATUS", "ECU reports ESTOP")
    )


def test_node_status_unlatched_reports_clear():
    msgs = [
        _msg(
            "SYS_NODE_STATUS",
            "low",
            0x500,
            {
                "command_received": 1,
                "command_nonzero": 0,
                "command_executing": 0,
                "command_rejected": 0,
                "system_ready": 1,
                "degraded": 0,
                "block_mask_low": 0,
                "block_mask_high": 0,
            },
        )
    ]
    r = build_estop_report(msgs, host_latch=False)
    assert r["active"] is False
    assert r["nodes"]["sys"]["state"] is None
    assert "clear" in r["summary"].lower()

    assert r["rt"]["frame_fresh"] is False


def test_rt_diag_event_rpt_ingested():
    msgs = [
        _msg(
            "RT_STATE_RPT",
            "high",
            0x210,
            {"mode": 2, "safety_state": 1, "estop_reason": 9},
        ),
        _msg(
            "RT_DIAG_EVENT_RPT",
            "high",
            0x621,
            {
                "diag_id": 0x0208,  # RtHostDriveCmdStale
                "state": 1,         # ACTIVE
                "occurrence_count": 3,
            },
            freshness=FreshnessState.LIVE,
            age_ms=50.0,
        ),
    ]
    r = build_estop_report(msgs, host_latch=False)
    assert r["active"] is True
    assert len(r["rt"]["diag_events"]) == 1
    ev = r["rt"]["diag_events"][0]
    assert ev["diag_id"] == 0x0208
    assert ev["state"] == "ACTIVE"
    assert ev["occurrences"] == 3
    assert "RT_HOST_DRIVE_CMD_STALE" in ev["key"]
    assert "RT_HOST_DRIVE_CMD_STALE (ACTIVE)" in r["primary_cause"]

