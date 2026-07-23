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
from typing import TYPE_CHECKING, Any, Callable

from control_toolkit import protocol_bridge as proto
from control_toolkit.models.frames import RawFrameEnvelope
from control_toolkit.models.state import FreshnessState, MessageState, SignalValue
from control_toolkit.pipeline.decoder import decode_envelope
from control_toolkit.pipeline.validator import validate_codec_status
from control_toolkit.services.vendor_field_layouts import field_meta_map
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
        on_frame: Callable[[RawFrameEnvelope], Any] | None = None,
        on_message: Callable[[MessageState, RawFrameEnvelope], Any] | None = None,
    ) -> None:
        self._transport = transport
        self._latest = latest
        self._history = history
        self._topology = topology
        self._on_frame = on_frame
        self._on_message = on_message
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
        if self._on_frame is not None:
            try:
                self._on_frame(env)
            except Exception:
                pass

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
            self._notify_message(state, env)
            return

        signals: dict[str, SignalValue] = {}
        if result.status == "ok" and result.signals:
            layout = proto.CATALOG[result.key].get("layout") or {}
            fields = field_meta_map(result.key, layout)
            for name, val in result.signals.items():
                # Opaque codecs may return a single "raw" blob — skip for signals UI
                if name == "raw" and isinstance(val, (bytes, bytearray)):
                    continue
                meta = fields.get(name, {})
                enum = meta.get("enum") if isinstance(meta.get("enum"), dict) else None
                raw: int | float | None = None
                if isinstance(val, bool):
                    raw = int(val)
                elif isinstance(val, (int, float)) and not isinstance(val, bool):
                    raw = val
                enum_label = (
                    enum.get(
                        str(
                            int(val)
                            if isinstance(val, float) and val == int(val)
                            else val
                        )
                    )
                    if enum
                    else None
                )
                if enum and enum_label is None and isinstance(val, (int, float)):
                    enum_label = enum.get(str(int(val)))
                eng = _coerce_value(val)
                # Apply catalog scale/offset when codec only returns raw integers
                factor = meta.get("factor")
                offset = meta.get("offset")
                if (
                    isinstance(raw, (int, float))
                    and not isinstance(val, bool)
                    and (factor is not None or offset is not None)
                    and meta.get("unit") not in (None, "", "raw")
                ):
                    f = float(factor if factor is not None else 1)
                    o = float(offset if offset is not None else 0)
                    eng = raw * f + o
                unit = meta.get("unit")
                if unit == "raw":
                    unit = None
                signals[name] = SignalValue(
                    raw_value=raw,
                    engineering_value=eng,
                    unit=unit,
                    enum_label=enum_label,
                    valid=True,
                )
            # Convenience aliases for UI meters (same scale as can-dictionary)
            if result.name == "SES_STATUS":
                ang = signals.get("steering_angle_raw")
                if ang is not None and isinstance(ang.raw_value, (int, float)):
                    deg = float(ang.raw_value) * 0.1 - 3000.0
                    signals["angle_deg"] = SignalValue(
                        raw_value=ang.raw_value,
                        engineering_value=deg,
                        unit="deg",
                        valid=True,
                    )
                tq = signals.get("steering_torque_raw")
                if tq is not None and isinstance(tq.raw_value, (int, float)):
                    nm = float(tq.raw_value) * 0.1 - 12.1
                    signals["torque_nm"] = SignalValue(
                        raw_value=tq.raw_value,
                        engineering_value=nm,
                        unit="Nm",
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
        self._notify_message(state, env)
        if self._topology is not None:
            self._topology.observe(state)

    def _notify_message(self, state: MessageState, env: RawFrameEnvelope) -> None:
        if self._on_message is None:
            return
        try:
            self._on_message(state, env)
        except Exception:
            # Observation-side diagnostics must never stop CAN routing.
            pass

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
