"""Aggregated settings snapshot for the Settings workspace.

Surfaces real backend state and config — not a static hardcoded page.
"""

from __future__ import annotations

from fastapi import APIRouter, Request

from control_toolkit import __version__, protocol_bridge as proto

router = APIRouter(prefix="/settings", tags=["settings"])


@router.get("")
def get_settings(request: Request) -> dict:
    """Full settings payload for UI: transport, session, adapter, protocol, runtime."""
    life = request.app.state.lifecycle
    config = request.app.state.config

    discovery = life.physical_discovery()
    ok, reason = discovery.available, discovery.reason

    if life.transport is not None:
        st = life.transport.status()
        adapter = (
            st.model_dump(mode="json")
            if hasattr(st, "model_dump")
            else dict(st) if isinstance(st, dict) else {"raw": str(st)}
        )
    else:
        adapter = {
            "identity": "none",
            "health": "absent",
            "adapter_epoch": 0,
            "capability": {},
            "channels": {},
        }

    session = life.sessions.snapshot().model_dump(mode="json")
    history_metrics = life.history.metrics() if hasattr(life, "history") else {}
    control = life.control.snapshot() if hasattr(life, "control") else {}
    peers = life.synthetic.list_running() if hasattr(life, "synthetic") else []
    diag_eps = (
        life.diagnostics.list_episodes() if hasattr(life, "diagnostics") else []
    )
    rec_active = None
    try:
        if hasattr(life, "recording"):
            # RecordingService.active is a method, not a property.
            active_fn = getattr(life.recording, "active", None)
            rec = active_fn() if callable(active_fn) else active_fn
            if rec is not None:
                if hasattr(rec, "to_summary"):
                    rec_active = rec.to_summary()
                elif hasattr(rec, "model_dump"):
                    rec_active = rec.model_dump(mode="json")
                else:
                    rec_active = {"id": getattr(rec, "id", None), "active": True}
    except Exception:
        rec_active = None

    dest = str(session.get("destination") or "virtual")
    # Channel map: architecture default CH0=High, CH1=Low (physical only)
    channel_map = {
        "high": {
            "logical": "high",
            "physical": "CH0" if dest == "physical" else "virtual:high",
            "bitrate": 500_000,
            "role": "Host / RT / SYS high bus",
        },
        "low": {
            "logical": "low",
            "physical": "CH1" if dest == "physical" else "virtual:low",
            "bitrate": 500_000,
            "role": "Motor / SES / SEB low bus",
        },
    }

    return {
        "service": {
            "title": config.title,
            "version": __version__,
            "ready": life.ready,
            "api_prefix": config.api_prefix,
            "host": config.host,
            "port": config.port,
            "workers": config.workers,
        },
        "transport": {
            "modes": [
                {
                    "id": "computer",
                    "label": "Computer (virtual)",
                    "description": (
                        "Same backend on this PC. Dual virtual High/Low buses — "
                        "no USB adapter."
                    ),
                    "destination": "virtual",
                    "profile": "pure_software",
                    "available": True,
                    "adapter": "none",
                },
                {
                    "id": "real",
                    "label": "Real (CANalyst-II)",
                    "description": (
                        "Physical High/Low via CANalyst-II (CH0=High, CH1=Low @ 500 kbit/s). "
                        "Refuses without the adapter — no silent virtual fallback."
                    ),
                    "destination": "physical",
                    "profiles": ["bench_test", "full_vehicle"],
                    "available": ok,
                    "reason": None if ok else reason,
                    "adapter": "canalystii",
                },
            ],
            "profiles": [
                {
                    "id": "pure_software",
                    "label": "Computer · Virtual buses",
                    "destination": "virtual",
                    "mode": "computer",
                    "available": True,
                },
                {
                    "id": "bench_test",
                    "label": "Real · Bench Test (CANalyst-II)",
                    "destination": "physical",
                    "mode": "real",
                    "available": ok,
                    "reason": None if ok else reason,
                },
                {
                    "id": "full_vehicle",
                    "label": "Real · Full Vehicle (CANalyst-II)",
                    "destination": "physical",
                    "mode": "real",
                    "available": ok,
                    "reason": None if ok else reason,
                },
            ],
            "physical_adapter": {
                **discovery.model_dump(),
                "kind": "canalystii",
                "available": ok,
                "reason": None if ok else reason,
                "channels": {"high": "CH0", "low": "CH1"},
            },
            "channel_map": channel_map,
            "active": {
                "profile": session.get("profile"),
                "destination": dest,
                "mode": (
                    "real"
                    if dest == "physical"
                    or str(session.get("profile") or "")
                    in ("bench_test", "full_vehicle")
                    else "computer"
                ),
            },
        },
        "session": session,
        "adapter": adapter,
        "protocol": {
            "wire_hash": proto.WIRE_HASH,
            "semantic_hash": proto.SEMANTIC_HASH,
            "network_hash": proto.NETWORK_HASH,
            "catalog": {
                "messages": proto.message_count(),
                "instances": proto.instance_count(),
            },
        },
        "runtime": {
            "default_profile": config.default_profile.value,
            "stream_heartbeat_ms": config.stream_heartbeat_ms,
            "latest_state_batch_hz": config.latest_state_batch_hz,
            "browser_degraded_ms": config.browser_degraded_ms,
            "browser_lost_ms": config.browser_lost_ms,
            "rx_queue_maxsize": config.rx_queue_maxsize,
            "history_capacity": config.history_capacity,
            "canalyst_device_index": config.canalyst_device_index,
            "canalyst_bitrate": config.canalyst_bitrate,
            "canalyst_poll_ms": config.canalyst_poll_ms,
            "canalyst_receive_timeout_ms": config.canalyst_receive_timeout_ms,
            "canalyst_reconnect_initial_ms": config.canalyst_reconnect_initial_ms,
            "canalyst_reconnect_max_ms": config.canalyst_reconnect_max_ms,
            "canalyst_recovery_stability_ms": config.canalyst_recovery_stability_ms,
            "host": config.host,
            "port": config.port,
            "env_prefix": "CTK_",
            "notes": (
                "Runtime knobs come from ToolkitConfig (CTK_* env). "
                "They are live values of this process — not a separate settings store."
            ),
        },
        "history": history_metrics,
        "control": control,
        "synthetic_peers": peers,
        "diagnostics": {
            "episode_count": len(diag_eps) if isinstance(diag_eps, list) else 0,
            "episodes": diag_eps[:20] if isinstance(diag_eps, list) else [],
        },
        "recording": {"active": rec_active},
    }
