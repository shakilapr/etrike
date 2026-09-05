"""Phase 0 contract audit: critical messages, forwarding, codec consumer path."""

from __future__ import annotations

from protocol.codecs.python import decode, encode
from protocol.codecs.python.types import Frame
from protocol.generated.python import etrike_protocol
from protocol.tools.protocol import load_model, validate_model


def test_no_legacy_dual_yaml_files():
    from pathlib import Path

    contracts = Path(__file__).resolve().parents[2] / "contracts"
    names = {p.name for p in contracts.glob("*.yaml")}
    assert "can_high.yaml" not in names
    assert "can_low.yaml" not in names
    assert "network.yaml" in names
    for required in (
        "host.yaml",
        "rt.yaml",
        "sys.yaml",
        "mtr.yaml",
        "ses.yaml",
        "seb.yaml",
        "hmi.yaml",
        "pwt.yaml",
    ):
        assert required in names


def test_message_count_and_strategies():
    assert len(etrike_protocol.METADATA) == 35
    strategies = {
        m["codec"]["strategy"] for m in etrike_protocol.METADATA.values()
    }
    assert strategies <= {"generated", "profile", "custom"}
    custom = [
        k
        for k, m in etrike_protocol.METADATA.items()
        if m["codec"]["strategy"] == "custom"
    ]
    assert any(k.startswith("ses:") for k in custom)
    assert any(k.startswith("seb:") for k in custom)


def test_toolkit_consumer_import_path():
    """Control Toolkit imports only generated + codecs — no re-parse of YAML."""
    # Round-trip a high-bus analysis frame the UI injects.
    st, payload = encode(
        "host:host_drive_cmd",
        {"speed_mmps": 500, "yaw_rate_mrad_s": 250, "gear": 1},
        bus="high",
    )
    assert st == "ok" and payload is not None
    frame = Frame("high", 0x300, "standard", payload.data)
    st2, values = decode("host:host_drive_cmd", frame)
    assert st2 == "ok"
    assert values["speed_mmps"] == 500
    assert values["yaw_rate_mrad_s"] == 250


def test_generated_encode_rejects_out_of_range():
    st, payload = etrike_protocol.encode(
        "host:host_drive_cmd",
        {"speed_mmps": 99999, "yaw_rate_mrad_s": 0, "gear": 0},
        bus="high",
    )
    assert st == "value_out_of_range"
    assert payload == b""


def test_baseline_validation_passes():
    validate_model(load_model(), check_baseline=True)


def test_discovery_manifest_hashes_match_runtime():
    import json
    from pathlib import Path

    discovery = json.loads(
        (Path(__file__).resolve().parents[2] / "generated" / "discovery.json").read_text(
            encoding="utf-8"
        )
    )
    assert discovery["semantic_hash"] == etrike_protocol.SEMANTIC_HASH
    assert discovery["wire_hash"] == etrike_protocol.WIRE_HASH
    assert discovery["network_hash"] == etrike_protocol.NETWORK_HASH
    assert len(discovery["messages"]) == 35
