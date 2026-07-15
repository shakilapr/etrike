"""Drive every High/Low golden vector through the live pipeline (workplan §1 exit gate).

For each vector in ``protocol/vectors/payload-v1.json`` on the High or Low bus,
inject the exact payload on the virtual transport and assert the router records
the same codec status the vector declares — proving all RT/SYS/MTR/SES/SEB
messages (generated and custom vendor codecs) survive inject -> router -> state.

PWT/powertrain vectors are excluded: the VTC adapter is a two-channel High/Low
tool (architecture §4.4), so the powertrain bus is out of scope here.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from vtc import protocol_bridge as proto
from vtc.models.frames import ChannelId
from vtc.pipeline.router import Router
from vtc.state.latest import LatestStore
from vtc.transport.virtual import VirtualTransportAdapter

_ROOT = Path(__file__).resolve().parents[3]
_VECTORS = json.loads(
    (_ROOT / "protocol" / "vectors" / "payload-v1.json").read_text(encoding="utf-8")
)["vectors"]
_HILO = [
    v
    for v in _VECTORS
    if v["bus"] in ("high", "low") and v["frame_format"] == "standard"
]


def _can_id(key: str, bus: str) -> int:
    for inst in proto.CATALOG[key]["instances"]:
        if inst["bus"] == bus:
            return int(inst["id"])
    raise KeyError((key, bus))


def _drain(router: Router, want: int = 1) -> int:
    got = 0
    for _ in range(30):
        n = router.drain_once(timeout=0.2 if got < want else 0.0)
        got += n
        if got >= want and n == 0:
            break
    return got


@pytest.fixture(scope="module")
def rig():
    adapter = VirtualTransportAdapter()
    adapter.open()
    latest = LatestStore()
    router = Router(adapter, latest)
    try:
        yield adapter, latest, router
    finally:
        adapter.close()


def test_every_highlow_message_has_a_vector():
    """Coverage guarantee: no High/Low catalog message is left undriven."""
    covered = {v["message"] for v in _HILO}
    highlow = {
        key
        for key, msg in proto.CATALOG.items()
        if any(i["bus"] in ("high", "low") for i in msg["instances"])
    }
    assert not (highlow - covered), f"no vector drives: {highlow - covered}"


@pytest.mark.parametrize("vec", _HILO, ids=[v["id"] for v in _HILO])
def test_vector_through_pipeline(rig, vec):
    adapter, latest, router = rig
    key, bus = vec["message"], vec["bus"]
    can_id = _can_id(key, bus)
    payload = bytes.fromhex(vec["payload"])

    adapter.inject(ChannelId(bus), can_id, payload)
    assert _drain(router) >= 1

    m = {(x.bus, x.can_id): x for x in latest.snapshot().messages}[(bus, can_id)]
    assert m.key == key
    assert m.validation_status == vec["status"], vec["id"]

    if vec["status"] == "ok":
        assert m.freshness.value == "live"
        cat = proto.CATALOG[key]
        # Full value round-trip for generated signal messages.
        if cat["codec"]["strategy"] == "generated" and cat["layout"]["kind"] == "signals":
            for field, expected in vec["values"].items():
                got = m.signals[field].engineering_value
                if isinstance(expected, float):
                    assert got == pytest.approx(expected), f"{vec['id']}:{field}"
                else:
                    assert got == expected, f"{vec['id']}:{field}"
    else:
        # Failure stays visible but fabricates no values (workplan §1.4).
        assert m.freshness.value == "invalid"
        assert m.signals == {}
