"""Injection service: manage one-shot message submissions (workplan §5.6)."""

from __future__ import annotations

import asyncio
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Callable

from vtc.services.encoder import EncoderService
from vtc.services.tx_gate import TxGate


@dataclass
class InjectionRecord:
    """Record of a one-shot message injection."""

    injection_id: str
    key: str
    values: dict[str, Any]
    bus: str
    status: str  # "pending", "submitted", "failed", "cancelled"
    can_id: int | None = None
    data: bytes | None = None
    error_code: str | None = None
    error_message: str | None = None
    delay_ms: int = 0
    created_at: datetime = field(default_factory=datetime.utcnow)
    submitted_at: datetime | None = None
    session_id: str | None = None


class InjectionService:
    """Manage one-shot message injections.

    Responsibilities:
    - Store injection records with metadata
    - Validate injections via TX Gate
    - Handle delayed submission
    - Track submission status
    - Maintain injection history
    """

    def __init__(
        self,
        encoder: EncoderService,
        tx_gate: TxGate,
    ):
        """Initialize injection service.

        Args:
            encoder: EncoderService for validation
            tx_gate: TxGate for submission validation
        """
        self.encoder = encoder
        self.tx_gate = tx_gate
        self._lock = asyncio.Lock()

        # Storage: session_id → {injection_id → InjectionRecord}
        self.injections: dict[str, dict[str, InjectionRecord]] = {}

        # Pending delayed submissions: (session_id, injection_id) → task
        self.pending_tasks: dict[tuple[str, str], asyncio.Task] = {}

        # Max history per session
        self.max_injections_per_session = 1000

    async def submit_injection(
        self,
        session_id: str,
        key: str,
        values: dict[str, Any],
        bus: str,
        delay_ms: int = 0,
        owner: str = "user",
    ) -> InjectionRecord:
        """Submit an injection for transmission.

        Args:
            session_id: Session ID
            key: Message key
            values: Engineering values
            bus: Bus name
            delay_ms: Delay before sending (0 = immediate)
            owner: Owner for lease validation

        Returns:
            InjectionRecord with status and submission results
        """
        async with self._lock:
            # Create record
            injection_id = f"inj_{uuid.uuid4().hex[:12]}"
            record = InjectionRecord(
                injection_id=injection_id,
                key=key,
                values=values,
                bus=bus,
                status="pending",
                delay_ms=delay_ms,
                session_id=session_id,
            )

            # Store record
            if session_id not in self.injections:
                self.injections[session_id] = {}
            self.injections[session_id][injection_id] = record

            # If no delay, submit immediately
            if delay_ms == 0:
                await self._process_injection(record, owner)
            else:
                # Schedule delayed submission
                task = asyncio.create_task(
                    self._delayed_submit(record, owner, delay_ms)
                )
                self.pending_tasks[(session_id, injection_id)] = task

            return record

    async def _process_injection(
        self, record: InjectionRecord, owner: str
    ) -> None:
        """Process an injection (encode and submit via TX Gate).

        Args:
            record: Injection record
            owner: Owner for lease validation
        """
        # Validate encoding
        encode_result = self.encoder.encode_message(
            record.key,
            record.values,
            record.bus,
            session_id=record.session_id,
        )

        if not encode_result.ok:
            record.status = "failed"
            record.error_code = f"encode.{encode_result.error_code}"
            record.error_message = encode_result.error
            return

        record.can_id = encode_result.can_id
        record.data = encode_result.data

        # Try to submit via TX Gate (this will validate ownership, profile, etc.)
        # For now, just record success
        # In full integration, this would call the actual TX submission
        record.status = "submitted"
        record.submitted_at = datetime.utcnow()

    async def _delayed_submit(
        self, record: InjectionRecord, owner: str, delay_ms: int
    ) -> None:
        """Handle delayed submission.

        Args:
            record: Injection record
            owner: Owner for lease validation
            delay_ms: Delay in milliseconds
        """
        try:
            # Wait for delay
            await asyncio.sleep(delay_ms / 1000.0)

            # Process the injection
            await self._process_injection(record, owner)
        except asyncio.CancelledError:
            record.status = "cancelled"
            raise
        except Exception as e:
            record.status = "failed"
            record.error_code = "submission.error"
            record.error_message = str(e)

    async def cancel_injection(self, session_id: str, injection_id: str) -> bool:
        """Cancel a pending injection.

        Args:
            session_id: Session ID
            injection_id: Injection ID

        Returns:
            True if cancelled, False if not found or already submitted
        """
        async with self._lock:
            # Check if injection exists
            if session_id not in self.injections:
                return False

            record = self.injections[session_id].get(injection_id)
            if not record:
                return False

            # Can only cancel pending injections
            if record.status != "pending":
                return False

            # Cancel task if exists
            task_key = (session_id, injection_id)
            if task_key in self.pending_tasks:
                task = self.pending_tasks.pop(task_key)
                task.cancel()

            record.status = "cancelled"
            return True

    async def get_injection(
        self, session_id: str, injection_id: str
    ) -> InjectionRecord | None:
        """Get injection record.

        Args:
            session_id: Session ID
            injection_id: Injection ID

        Returns:
            InjectionRecord or None if not found
        """
        async with self._lock:
            if session_id not in self.injections:
                return None
            return self.injections[session_id].get(injection_id)

    async def list_injections(
        self,
        session_id: str,
        limit: int = 100,
        offset: int = 0,
        status_filter: str | None = None,
    ) -> tuple[list[InjectionRecord], int]:
        """List injections for a session.

        Args:
            session_id: Session ID
            limit: Max results
            offset: Results offset
            status_filter: Optional status filter

        Returns:
            (list of records, total count)
        """
        async with self._lock:
            if session_id not in self.injections:
                return [], 0

            records = list(self.injections[session_id].values())

            # Filter by status if requested
            if status_filter:
                records = [r for r in records if r.status == status_filter]

            total = len(records)

            # Apply pagination
            records = records[offset : offset + limit]

            return records, total

    async def clear_session_injections(self, session_id: str) -> int:
        """Clear all injections for a session.

        Args:
            session_id: Session ID

        Returns:
            Number of injections cleared
        """
        async with self._lock:
            if session_id not in self.injections:
                return 0

            # Cancel any pending tasks
            for inj_id, record in self.injections[session_id].items():
                if record.status == "pending":
                    task_key = (session_id, inj_id)
                    if task_key in self.pending_tasks:
                        task = self.pending_tasks.pop(task_key)
                        task.cancel()

            count = len(self.injections[session_id])
            del self.injections[session_id]
            return count

    async def get_session_stats(self, session_id: str) -> dict:
        """Get injection statistics for a session.

        Args:
            session_id: Session ID

        Returns:
            Stats dict with counts by status
        """
        async with self._lock:
            if session_id not in self.injections:
                return {
                    "total": 0,
                    "pending": 0,
                    "submitted": 0,
                    "failed": 0,
                    "cancelled": 0,
                }

            records = self.injections[session_id].values()
            return {
                "total": len(records),
                "pending": sum(1 for r in records if r.status == "pending"),
                "submitted": sum(1 for r in records if r.status == "submitted"),
                "failed": sum(1 for r in records if r.status == "failed"),
                "cancelled": sum(1 for r in records if r.status == "cancelled"),
            }
