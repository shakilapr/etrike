"""Observation router (workplan §1.4).

Drains the transport RX queue and, per frame:
  1. assigns a monotonic global sequence,
  2. decodes via the generated codec (through the bridge),
  3. records the validation status,
  4. updates the latest-value store,
  5. sets a first-cut freshness (full aging lives in §1.5).

Runs off the ASGI event loop: the blocking ``poll`` executes in a worker thread
(``asyncio.to_thread``); processing is fast and lock-guarded by the store.

Invariants (workplan §1.4):
  - Unknown frames stay visible — no guessed decoding.
  - Decode failure preserves the raw frame and fabricates no values.
"""

from __future__ import annotations

import asyncio

from vtc import protocol_bridge as proto
from vtc.models.frames import RawFrameEnvelope
from vtc.models.state import FreshnessState, MessageState, SignalValue
from vtc.pipeline.decoder import decode_envelope
from vtc.state.latest import LatestStore
from vtc.transport.interface import Transport


def _coerce_value(v: object) -> int | float | str | None:
    if isinstance(v, (int, float, str)) or v is None:
        return v
    if isinstance(v, (bytes, bytearray)):
        return v.hex()
    return str(v)


class Router:
    def __init__(self, transport: Transport, latest: LatestStore) -> None:
        self._transport = transport
        self._latest = latest
        self._seq = 0
        self._running = False

    @property
    def sequence(self) -> int:
        """Highest global sequence assigned so far."""
        return self._seq

    # ---- per-frame processing ------------------------------------------------

    def process(self, env: RawFrameEnvelope) -> None:
        self._seq += 1
        env = env.model_copy(update={"global_sequence": self._seq})
        bus = env.channel.value
        result = decode_envelope(env)

        if not result.is_known:
            # Unknown frame stays visible with its raw identity, no decoded values.
            self._latest.upsert(
                MessageState(
                    bus=bus,
                    can_id=env.can_id,
                    key=None,
                    name="UNKNOWN",
                    last_seen_ns=env.backend_arrival_ns,
                    freshness=FreshnessState.LIVE,
                    validation_status="unknown_id",
                )
            )
            return

        signals: dict[str, SignalValue] = {}
        if result.status == "ok" and result.signals:
            fields = {
                f["key"]: f
                for f in proto.CATALOG[result.key]["layout"].get("fields", [])
            }
            for name, val in result.signals.items():
                meta = fields.get(name, {})
                enum = meta.get("enum") if isinstance(meta.get("enum"), dict) else None
                enum_label = enum.get(str(val)) if enum else None
                signals[name] = SignalValue(
                    engineering_value=_coerce_value(val),
                    unit=meta.get("unit"),
                    enum_label=enum_label,
                    valid=True,
                )

        # 'ok' -> Live; any codec error (range/checksum/enum/…) -> Invalid, but the
        # frame and its status stay visible.
        freshness = (
            FreshnessState.LIVE if result.status == "ok" else FreshnessState.INVALID
        )
        inst = proto.instance_for(bus, env.can_id)
        cycle_ms = int(inst["cycle_ms"]) if inst else 0
        expected_rate = 1000.0 / cycle_ms if cycle_ms > 0 else None

        self._latest.upsert(
            MessageState(
                bus=bus,
                can_id=env.can_id,
                key=result.key,
                name=result.name,
                last_seen_ns=env.backend_arrival_ns,
                expected_rate_hz=expected_rate,
                freshness=freshness,
                validation_status=result.status,
                signals=signals,
            )
        )

    # ---- draining ------------------------------------------------------------

    def drain_once(self, timeout: float = 0.0) -> int:
        """Poll the transport once and process what is available. Returns count."""
        frames = self._transport.poll(timeout=timeout)
        for env in frames:
            self.process(env)
        return len(frames)

    async def run(self, poll_timeout: float = 0.05) -> None:
        """Continuously drain until :meth:`stop`. Blocking poll runs in a thread."""
        self._running = True
        while self._running:
            n = await asyncio.to_thread(self.drain_once, poll_timeout)
            if n == 0:
                await asyncio.sleep(0.001)  # yield when idle

    def stop(self) -> None:
        self._running = False
