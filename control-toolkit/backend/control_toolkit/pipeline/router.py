"""Observation router (workplan §1.4).

Drains the transport RX queue and, per frame:
  1. assign global sequence
  2. append to bounded history
  3. decode via generated/custom codec
  4. validate
  5. update latest-value store + topology
  6. freshness (initial; ager continues aging)

Invariants:
  - Unknown frames stay visible — no guessed decoding.
  - Decode failure preserves the raw frame and fabricates no values.
  - No decode in the transport receive callback (only here).
"""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from control_toolkit import protocol_bridge as proto
from control_toolkit.models.frames import RawFrameEnvelope
from control_toolkit.models.state import FreshnessState, MessageState, SignalValue
from control_toolkit.pipeline.decoder import decode_envelope
from control_toolkit.pipeline.validator import validate_codec_status
from control_toolkit.state.latest import LatestStore
from control_toolkit.transport.interface import Transport

if TYPE_CHECKING:
    from control_toolkit.state.history import FrameHistory
    from control_toolkit.state.topology import TopologyTracker


def _coerce_value(v: object) -> int | float | str | None:
    if isinstance(v, (int, float, str)) or v is None:
        return v
    if isinstance(v, (bytes, bytearray)):
        return v.hex()
    return str(v)


class Router:
    def __init__(
        self,
        transport: Transport,
        latest: LatestStore,
        *,
        history: FrameHistory | None = None,
        topology: TopologyTracker | None = None,
    ) -> None:
        self._transport = transport
        self._latest = latest
        self._history = history
        self._topology = topology
        self._seq = 0
        self._running = False
        self._processed = 0

    @property
    def sequence(self) -> int:
        return self._seq

    @property
    def processed_count(self) -> int:
        return self._processed

    def process(self, env: RawFrameEnvelope) -> None:
        self._seq += 1
        self._processed += 1
        env = env.model_copy(update={"global_sequence": self._seq})
        bus = env.channel.value

        if self._history is not None:
            self._history.append(env)

        result = decode_envelope(env)

        if not result.is_known:
            state = MessageState(
                bus=bus,
                can_id=env.can_id,
                key=None,
                name="UNKNOWN",
                last_seen_ns=env.backend_arrival_ns,
                freshness=FreshnessState.LIVE,
                validation_status="unknown_id",
                signals={},
            )
            self._latest.upsert(state)
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
                raw: int | float | None = None
                if isinstance(val, bool):
                    raw = int(val)
                elif isinstance(val, (int, float)) and not isinstance(val, bool):
                    raw = val
                enum_label = enum.get(str(int(val) if isinstance(val, float) and val == int(val) else val)) if enum else None
                if enum and enum_label is None and isinstance(val, (int, float)):
                    enum_label = enum.get(str(int(val)))
                signals[name] = SignalValue(
                    raw_value=raw,
                    engineering_value=_coerce_value(val),
                    unit=meta.get("unit"),
                    enum_label=enum_label,
                    valid=True,
                )

        validation = validate_codec_status(result.status)
        freshness = (
            FreshnessState.LIVE if validation.ok else FreshnessState.INVALID
        )
        inst = proto.instance_for(bus, env.can_id)
        cycle_ms = int(inst["cycle_ms"]) if inst else 0
        expected_rate = 1000.0 / cycle_ms if cycle_ms > 0 else None

        state = MessageState(
            bus=bus,
            can_id=env.can_id,
            key=result.key,
            name=result.name,
            last_seen_ns=env.backend_arrival_ns,
            expected_rate_hz=expected_rate,
            freshness=freshness,
            validation_status=validation.status,
            signals=signals,
        )
        self._latest.upsert(state)
        if self._topology is not None:
            self._topology.observe(state)

    def drain_once(self, timeout: float = 0.0) -> int:
        frames = self._transport.poll(timeout=timeout)
        for env in frames:
            self.process(env)
        return len(frames)

    async def run(self, poll_timeout: float = 0.05) -> None:
        self._running = True
        while self._running:
            n = await asyncio.to_thread(self.drain_once, poll_timeout)
            if n == 0:
                await asyncio.sleep(0.001)

    def stop(self) -> None:
        self._running = False
