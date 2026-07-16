"""Central TX gate: profile, Bench TX, ownership, and encode before submit."""

from __future__ import annotations

import time
import uuid
from dataclasses import dataclass
from typing import Any

from control_toolkit.config import Profile
from control_toolkit.models.frames import ChannelId, Direction, FrameSource, RawFrameEnvelope
from control_toolkit.models.session import BenchTxState
from control_toolkit.services.encoder import EncodeResult, encode_message
from control_toolkit.services.ownership import OwnershipConflict, OwnershipTable
from control_toolkit.transport.virtual import VirtualTransportAdapter


@dataclass(frozen=True, slots=True)
class TxResult:
    disposition: str  # rejected | accepted | submitted | canceled | failed
    request_id: str
    reason: str | None = None
    encode: EncodeResult | None = None
    lease_id: str | None = None


class TxGate:
    def __init__(
        self,
        *,
        ownership: OwnershipTable,
        get_profile: Any,
        get_bench_tx: Any,
        get_transport: Any,
        get_adapter_epoch: Any,
    ) -> None:
        self._ownership = ownership
        self._get_profile = get_profile
        self._get_bench_tx = get_bench_tx
        self._get_transport = get_transport
        self._get_adapter_epoch = get_adapter_epoch

    def release_owner(self, owner: str) -> int:
        """Drop all TX leases held by ``owner`` (e.g. control release)."""
        return self._ownership.release_owner(owner)

    def submit(
        self,
        *,
        bus: str,
        values: dict[str, Any] | None = None,
        key: str | None = None,
        can_id: int | None = None,
        owner: str = "injection",
        source: FrameSource = FrameSource.INJECTION,
        claim_ownership: bool = True,
        lease_ttl_s: float = 5.0,
    ) -> TxResult:
        request_id = f"req_{uuid.uuid4().hex[:10]}"
        profile: Profile = self._get_profile()
        if profile not in (Profile.PURE_SOFTWARE, Profile.BENCH_TEST, Profile.FULL_VEHICLE):
            return TxResult("rejected", request_id, reason="unknown_profile")

        # Physical profiles require a real adapter (no silent virtual).
        transport = self._get_transport()
        if transport is None:
            return TxResult("rejected", request_id, reason="no_transport")

        if profile is not Profile.PURE_SOFTWARE:
            # Allow TX only when transport is physical canalyst (not virtual).
            identity = ""
            try:
                identity = str(transport.status().identity)
            except Exception:
                identity = ""
            if "canalyst" not in identity.lower():
                return TxResult(
                    "rejected",
                    request_id,
                    reason="physical_profile_unavailable",
                )

        if self._get_bench_tx() is not BenchTxState.ENABLED:
            return TxResult("rejected", request_id, reason="bench_tx_disabled")

        encoded = encode_message(key=key, bus=bus, can_id=can_id, values=values)
        if not encoded.ok:
            return TxResult(
                "rejected",
                request_id,
                reason=f"encode:{encoded.status}",
                encode=encoded,
            )

        lease_id = None
        if claim_ownership:
            try:
                lease = self._ownership.claim(
                    bus=encoded.bus,
                    can_id=encoded.can_id,
                    owner=owner,
                    ttl_s=lease_ttl_s,
                )
                lease_id = lease.lease_id
            except OwnershipConflict as exc:
                return TxResult(
                    "rejected",
                    request_id,
                    reason=f"ownership:{exc}",
                    encode=encoded,
                )

        channel = ChannelId(encoded.bus) if encoded.bus in ("high", "low") else None
        if channel is None:
            return TxResult(
                "rejected",
                request_id,
                reason="unsupported_bus",
                encode=encoded,
                lease_id=lease_id,
            )

        epoch = self._get_adapter_epoch() or 0
        env = RawFrameEnvelope(
            adapter_epoch=epoch,
            channel=channel,
            backend_arrival_ns=time.monotonic_ns(),
            can_id=encoded.can_id,
            is_extended=encoded.is_extended,
            dlc=encoded.dlc,
            data=encoded.data,
            channel_sequence=0,
            direction=Direction.TX,
            source=source,
        )
        try:
            disposition = transport.send(env)
        except Exception as exc:  # pragma: no cover
            return TxResult(
                "failed",
                request_id,
                reason=str(exc),
                encode=encoded,
                lease_id=lease_id,
            )
        # Virtual path: submitted frames are also observed as RX via emit bus.
        return TxResult(
            "submitted" if disposition == "submitted" else disposition,
            request_id,
            encode=encoded,
            lease_id=lease_id,
        )
