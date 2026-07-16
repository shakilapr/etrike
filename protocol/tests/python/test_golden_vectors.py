"""Phase 0 golden encode/decode vectors (workplan §0.4).

Runs language-neutral vectors from ``protocol/vectors/`` through the generated
Python codec and custom SES/SEB codecs. Exit criteria: every message has a
success vector; generated messages round-trip; negative cases fail as declared.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from protocol.codecs.python import decode as codec_decode
from protocol.codecs.python import encode as codec_encode
from protocol.codecs.python import seb, ses
from protocol.codecs.python.types import Frame
from protocol.generated.python import etrike_protocol
from protocol.tools.protocol import hashes, load_model, validate_model

ROOT = Path(__file__).resolve().parents[2]
VECTORS = ROOT / "vectors"

# Toolkit / architecture critical IDs (bus, can_id) → expected message name.
CRITICAL_IDS = {
    ("high", 0x001): "SAFETY_ESTOP",
    ("low", 0x001): "SAFETY_ESTOP",
    ("high", 0x111): "HMI_MODE_REQ",
    ("high", 0x112): "HMI_PWR_REQ",
    ("low", 0x201): "SES_STATUS",
    ("low", 0x206): "MTR_MOTOR_FBK",
    ("high", 0x300): "HOST_DRIVE_CMD",
    ("high", 0x310): "STEER_DIAG",
    ("high", 0x311): "BRAKE_DIAG",
    ("low", 0x600): "SYS_DIAG_RPT",
    ("high", 0x7FC): "HOST_HEARTBEAT",
    ("high", 0x7FD): "RT_HEARTBEAT",
    ("low", 0x7FD): "RT_HEARTBEAT",
    ("low", 0x7FE): "SYS_HEARTBEAT",
    ("low", 0x721): "SEB_STATUS",
    ("low", 0x7B9): "VCU_SEB_REQ",
    ("low", 0x169): "VCU_SES_REQ",
}


def _payload_doc() -> dict:
    return json.loads((VECTORS / "payload-v1.json").read_text(encoding="utf-8"))


def _index_messages() -> dict[tuple[str, int], dict]:
    index: dict[tuple[str, int], dict] = {}
    for key, msg in etrike_protocol.METADATA.items():
        for inst in msg["instances"]:
            index[(inst["bus"], int(inst["id"]))] = {
                "key": key,
                "name": msg["name"],
                **msg,
                "instance": inst,
            }
    return index


@pytest.fixture(scope="module")
def model():
    m = load_model()
    return validate_model(m)


@pytest.fixture(scope="module")
def payload_vectors():
    return _payload_doc()["vectors"]


def test_every_catalog_message_has_success_vector(payload_vectors):
    covered = {
        v["message"]
        for v in payload_vectors
        if v["status"] in {"ok", "unsupported_semantics"}
    }
    assert covered == set(etrike_protocol.METADATA.keys())


@pytest.mark.parametrize(
    "vector",
    _payload_doc()["vectors"],
    ids=lambda v: v["id"],
)
def test_payload_vector(vector):
    payload = bytes.fromhex(vector["payload"])
    status, decoded = etrike_protocol.decode(
        vector["message"],
        payload,
        bus=vector["bus"],
        frame_format=vector["frame_format"],
    )
    assert status == vector["status"]
    if status != "ok" or "values" not in vector:
        return
    assert decoded is not None
    for key, expected in vector["values"].items():
        assert decoded[key] == pytest.approx(expected)

    # Generated strategies must encode back to the golden payload.
    meta = etrike_protocol.METADATA[vector["message"]]
    if meta["codec"]["strategy"] == "generated":
        enc_status, encoded = etrike_protocol.encode(
            vector["message"],
            vector["values"],
            bus=vector["bus"],
            frame_format=vector["frame_format"],
        )
        assert enc_status == "ok"
        assert encoded == payload


def test_dlc_zero_estop_both_buses():
    for bus in ("high", "low"):
        st, pl = etrike_protocol.encode(
            "safety:safety_estop", {}, bus=bus, frame_format="standard"
        )
        assert st == "ok"
        assert pl == b""
        st2, val = etrike_protocol.decode(
            "safety:safety_estop", b"", bus=bus, frame_format="standard"
        )
        assert st2 == "ok"
        assert val == {}


def test_rt_heartbeat_independent_instances():
    """Same ID on High and Low are separate runtime identities."""
    hi = etrike_protocol.METADATA["rt:rt_heartbeat"]["instances"]
    buses = {i["bus"] for i in hi}
    assert buses == {"high", "low"}
    for bus in ("high", "low"):
        st, pl = etrike_protocol.encode(
            "rt:rt_heartbeat",
            {"alive_ctr": 1, "health_flags": 0},
            bus=bus,
        )
        assert st == "ok"
        st2, val = etrike_protocol.decode("rt:rt_heartbeat", pl, bus=bus)
        assert st2 == "ok"
        assert val["alive_ctr"] == 1


def test_critical_toolkit_ids_resolve():
    index = _index_messages()
    for identity, name in CRITICAL_IDS.items():
        assert identity in index, f"missing {identity}"
        assert index[identity]["name"] == name


def test_network_routes_match_rt_gateway_expectations(model):
    """network.yaml routes must cover the RT forward set used by firmware."""
    routes = model["network"]["routes"]
    pairs = {(r["from"], r["to"], r["message"]) for r in routes}

    # From can_rx_router comments + network.yaml (expanded HMI).
    required = {
        ("low", "high", "safety:safety_estop"),
        ("high", "low", "safety:safety_estop"),
        ("low", "high", "sys:sys_safety_sts"),
        ("low", "high", "mtr:sys_throttle_sts"),
        ("low", "high", "mtr:mtr_motor_fbk"),
        ("low", "high", "sys:sys_diag_rpt"),
        ("high", "low", "host:host_light_cmd"),
        ("high", "low", "hmi:hmi_mode_req"),
        ("high", "low", "hmi:hmi_pwr_req"),
    }
    assert required.issubset(pairs)

    for r in routes:
        assert r["semantics"] == "same_frame"


@pytest.mark.parametrize(
    "vector",
    json.loads((VECTORS / "custom-codec-values-v1.json").read_text(encoding="utf-8"))[
        "vectors"
    ],
    ids=lambda v: v["id"],
)
def test_custom_codec_value_roundtrip(vector):
    """SES/SEB custom codecs: values → frame → values."""
    key = vector["message"]
    values = vector["values"]
    st, frame = codec_encode(key, values, bus=vector["bus"])
    assert st == "ok" and frame is not None
    assert len(frame.data) == 8
    st2, decoded = codec_decode(key, frame)
    assert st2 == "ok" and decoded is not None
    for k, expected in values.items():
        assert decoded[k] == expected


def test_ses_command_checksum_integrity():
    st, frame = ses.encode_command(
        {
            "alignment_enable": True,
            "control_enable": True,
            "target_angle_raw": 0,
            "target_speed_raw": 328,
            "rolling_counter": 1,
            "vehicle_speed_raw": 0,
        }
    )
    assert st == "ok"
    bad = bytearray(frame.data)
    bad[7] ^= 0xFF
    st2, _ = ses.decode_command(Frame("low", 0x169, "standard", bytes(bad)))
    assert st2 == "checksum_mismatch"


def test_seb_command_checksum_integrity():
    st, frame = seb.encode_command(
        {
            "alignment_enable": True,
            "control_enable": True,
            "control_mode": 1,
            "auto_brake": False,
            "stroke_request_raw": 0,
            "pressure_request_raw": 10,
            "rolling_counter": 2,
        }
    )
    assert st == "ok"
    bad = bytearray(frame.data)
    bad[7] ^= 0x01
    st2, _ = seb.decode_command(Frame("low", 0x7B9, "standard", bytes(bad)))
    assert st2 == "checksum_mismatch"


def test_semantic_hash_deterministic_and_exported(model):
    m = load_model()
    v = validate_model(m)
    h1, n1 = hashes(m, v)
    h2, n2 = hashes(load_model(), validate_model(load_model()))
    assert h1 == h2
    assert n1 == n2
    assert etrike_protocol.SEMANTIC_HASH == h1
    assert etrike_protocol.WIRE_HASH == etrike_protocol.SEMANTIC_HASH
    assert etrike_protocol.NETWORK_HASH == n1
    assert re.fullmatch(r"[0-9a-f]{64}", etrike_protocol.SEMANTIC_HASH)
    assert re.fullmatch(r"[0-9a-f]{64}", etrike_protocol.NETWORK_HASH)


def test_typescript_semantic_hash_matches_python():
    ts = (ROOT / "generated" / "typescript" / "etrike-protocol.ts").read_text(
        encoding="utf-8"
    )
    m = re.search(r'export const SEMANTIC_HASH = "([0-9a-f]{64})"', ts)
    assert m, "SEMANTIC_HASH missing from TypeScript catalog"
    assert m.group(1) == etrike_protocol.SEMANTIC_HASH
    m2 = re.search(r'export const NETWORK_HASH = "([0-9a-f]{64})"', ts)
    assert m2
    assert m2.group(1) == etrike_protocol.NETWORK_HASH
    # WIRE_HASH is alias of SEMANTIC_HASH
    assert "export const WIRE_HASH = SEMANTIC_HASH" in ts


def test_cpp_hashes_match_python():
    hpp = (ROOT / "generated" / "cpp" / "etrike_protocol.hpp").read_text(encoding="utf-8")
    assert f'kSemanticHash = "{etrike_protocol.SEMANTIC_HASH}"' in hpp
    assert f'kNetworkHash = "{etrike_protocol.NETWORK_HASH}"' in hpp
    assert "kWireHash = kSemanticHash" in hpp


def test_generate_check_python_and_typescript_and_all():
    import subprocess
    import sys

    repo_root = ROOT.parent  # monorepo root (…/etrike)
    for args in (
        ["generate", "--check"],
        ["generate", "python", "--check"],
        ["generate", "typescript", "--check"],
    ):
        proc = subprocess.run(
            [sys.executable, "-m", "protocol.tools.protocol", *args],
            cwd=str(repo_root),
            capture_output=True,
            text=True,
            check=False,
        )
        assert proc.returncode == 0, proc.stderr + proc.stdout
        assert "SEMANTIC_HASH=" in proc.stdout
        assert "NETWORK_HASH=" in proc.stdout
        assert etrike_protocol.SEMANTIC_HASH in proc.stdout


def test_counter_metadata_present_on_heartbeats():
    for key in (
        "host:host_heartbeat",
        "rt:rt_heartbeat",
        "sys:sys_heartbeat",
    ):
        fields = {
            f["key"]: f for f in etrike_protocol.METADATA[key]["layout"]["fields"]
        }
        assert "alive_ctr" in fields
        assert "counter" in fields["alive_ctr"]
        assert fields["alive_ctr"]["counter"]["kind"] in {"wrapping", "modulo", "saturating"}


def test_sys_diag_rx_overflow_field():
    fields = {
        f["key"]: f
        for f in etrike_protocol.METADATA["sys:sys_diag_rpt"]["layout"]["fields"]
    }
    assert fields["rx_overflow"]["bits"] == 6
    assert fields["rx_overflow"].get("max", 63) == 63
