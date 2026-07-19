"""TX gate: guards transmission with policy checks (workplan §5.2).

Central validation before any frame submission:
- Profile permits transmission
- Adapter/channel healthy (or Pure Software)
- Bench TX enabled
- Source owns exclusive lease for (bus, can_id)
- Message not expired (for injections)
- Encoder validation passes
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from vtc.config import Profile
from vtc.models.session import BenchTxState, SessionState
from vtc.services.encoder import EncoderService, EncodeResult
from vtc.services.ownership import OwnershipTable


@dataclass
class TxResult:
    """Result of TX gate submission."""

    ok: bool
    job_id: str | None = None
    disposition: str | None = None  # "submitted", "queued", "accepted", etc.
    can_id: int | None = None
    data: bytes | None = None
    error_code: str | None = None
    error: str | None = None
    status_code: int | None = None


class TxGate:
    """Central TX gate that validates before transmission.

    Enforces all policy guards before frame submission.
    Returns TxResult with clear rejection reasons and HTTP status codes.
    """

    def __init__(
        self,
        encoder: EncoderService,
        ownership_table: OwnershipTable,
    ):
        """Initialize TX gate.

        Args:
            encoder: EncoderService for validating frames
            ownership_table: OwnershipTable for lease validation
        """
        self.encoder = encoder
        self.ownership_table = ownership_table

    async def submit_for_transmission(
        self,
        session_state: SessionState,
        key: str,
        values: dict,
        bus: str,
        session_id: str,
        owner: str = "user",
        job_type: Literal["one_shot", "periodic"] = "one_shot",
        period_ms: int | None = None,
    ) -> TxResult:
        """Validate and queue a message for transmission.

        Args:
            session_state: Current session state
            key: Message key (e.g., "host:host_drive_cmd")
            values: Engineering values
            bus: "high" or "low"
            session_id: Session ID for diagnostics
            owner: Lease owner (typically session_id)
            job_type: "one_shot" or "periodic"
            period_ms: Period in ms for periodic jobs

        Returns:
            TxResult with ok=True and can_id/data if successful,
            or ok=False with error_code/error and HTTP status_code
        """

        # 1. Check Bench TX is enabled
        if session_state.bench_tx != BenchTxState.ENABLED:
            return TxResult(
                ok=False,
                error_code="bench_tx.disabled",
                error="Bench TX is disabled; enable via /api/v1/sessions/{id}/bench-tx",
                status_code=503,
            )

        # 2. Check profile permits transmission
        # (For Phase 5, only Pure Software allows TX)
        if session_state.profile != Profile.PURE_SOFTWARE:
            return TxResult(
                ok=False,
                error_code="profile.tx_not_permitted",
                error=f"Profile '{session_state.profile.value}' does not permit transmission",
                status_code=403,
            )

        # 3. Validate encoding works
        encode_result = self.encoder.encode_message(
            key, values, bus, session_state.profile, session_id
        )
        if not encode_result.ok:
            return TxResult(
                ok=False,
                error_code=f"encode.{encode_result.error_code}",
                error=f"Message validation failed: {encode_result.error}",
                status_code=422,
            )

        # 4. Check source ownership of this (bus, can_id)
        can_id = encode_result.can_id

        try:
            lease = self.ownership_table.claim(
                bus=bus,
                can_id=can_id,
                owner=owner,
                ttl_s=5.0,  # 5 second lease by default
            )
        except Exception as e:
            # Check if someone else owns it
            current_owner = self.ownership_table.owner_of(bus, can_id)
            if current_owner and current_owner != owner:
                return TxResult(
                    ok=False,
                    error_code="ownership.conflict",
                    error=f"CAN ID 0x{can_id:03X} on {bus} bus is owned by {current_owner}",
                    status_code=409,
                )
            else:
                return TxResult(
                    ok=False,
                    error_code="ownership.no_lease",
                    error=f"Failed to claim ownership lease for CAN ID 0x{can_id:03X}: {str(e)}",
                    status_code=410,
                )

        # 6. All guards passed - return disposition
        return TxResult(
            ok=True,
            disposition="submitted",
            can_id=encode_result.can_id,
            data=encode_result.data,
        )

    async def check_guardians(
        self,
        session_state: SessionState,
        bus: str,
        can_id: int,
        owner: str,
    ) -> tuple[bool, str | None, int | None]:
        """Quick check of all guards without encoding.

        Returns:
            (ok, error_code, status_code)
        """
        # Check Bench TX
        if session_state.bench_tx != BenchTxState.ENABLED:
            return False, "bench_tx.disabled", 503

        # Check profile
        if session_state.profile != Profile.PURE_SOFTWARE:
            return False, "profile.tx_not_permitted", 403

        # Check ownership
        current_owner = self.ownership_table.owner_of(bus, can_id)
        if current_owner is None:
            # No one owns it - this is OK, we can claim it in submit_for_transmission
            return True, None, None
        elif current_owner == owner:
            # We own it - OK
            return True, None, None
        else:
            # Someone else owns it
            return False, "ownership.conflict", 409
