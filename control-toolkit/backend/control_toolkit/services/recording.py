"""Opt-in session recording with evidence quality (Phase 6 software track)."""

from __future__ import annotations

import threading
import time
import uuid
from collections import deque
from dataclasses import dataclass, field
from enum import Enum
from typing import Any


class EvidenceQuality(str, Enum):
    COMPLETE = "complete"
    DEGRADED = "degraded"
    INCOMPLETE = "incomplete"
    NOT_COMPARABLE = "not_comparable"


class RecordingState(str, Enum):
    IDLE = "idle"
    RECORDING = "recording"
    STOPPED = "stopped"


@dataclass
class RecordedFrame:
    seq: int
    bus: str
    can_id: int
    dlc: int
    data_hex: str
    direction: str
    source: str
    backend_arrival_ns: int
    adapter_epoch: int | None


@dataclass
class RecordingSession:
    recording_id: str
    state: RecordingState = RecordingState.RECORDING
    started_mono: float = field(default_factory=time.monotonic)
    stopped_mono: float | None = None
    frames: deque[RecordedFrame] = field(default_factory=lambda: deque(maxlen=50_000))
    dropped: int = 0
    capacity: int = 50_000
    evidence_quality: EvidenceQuality = EvidenceQuality.COMPLETE
    protocol_wire_hash: str | None = None
    notes: list[str] = field(default_factory=list)

    def to_summary(self) -> dict[str, Any]:
        return {
            "recording_id": self.recording_id,
            "state": self.state.value,
            "frame_count": len(self.frames),
            "dropped": self.dropped,
            "capacity": self.capacity,
            "evidence_quality": self.evidence_quality.value,
            "duration_s": (self.stopped_mono or time.monotonic()) - self.started_mono,
            "wire_hash": self.protocol_wire_hash,
            "notes": list(self.notes),
        }


class RecordingService:
    """In-process ring buffer recorder (virtual-first; disk export later)."""

    def __init__(self, capacity: int = 50_000) -> None:
        self._lock = threading.Lock()
        self._capacity = capacity
        self._active: RecordingSession | None = None
        self._history: list[RecordingSession] = []
        self._seq = 0

    def start(self, *, wire_hash: str | None = None) -> RecordingSession:
        with self._lock:
            if self._active is not None and self._active.state is RecordingState.RECORDING:
                raise RuntimeError("recording already active")
            rec = RecordingSession(
                recording_id=f"rec_{uuid.uuid4().hex[:12]}",
                frames=deque(maxlen=self._capacity),
                capacity=self._capacity,
                protocol_wire_hash=wire_hash,
            )
            self._active = rec
            return rec

    def stop(self, recording_id: str | None = None) -> RecordingSession | None:
        with self._lock:
            if self._active is None:
                return None
            if recording_id and self._active.recording_id != recording_id:
                return None
            rec = self._active
            rec.state = RecordingState.STOPPED
            rec.stopped_mono = time.monotonic()
            if rec.dropped > 0:
                rec.evidence_quality = EvidenceQuality.INCOMPLETE
                rec.notes.append("frame drops while recording")
            self._history.append(rec)
            self._active = None
            return rec

    def active(self) -> RecordingSession | None:
        with self._lock:
            return self._active

    def list_recordings(self) -> list[dict[str, Any]]:
        with self._lock:
            out = [r.to_summary() for r in self._history[-50:]]
            if self._active is not None:
                out.insert(0, self._active.to_summary())
            return out

    def get(self, recording_id: str) -> dict[str, Any] | None:
        return self.get_window(recording_id, offset=0, limit=500)

    def get_window(
        self,
        recording_id: str,
        *,
        offset: int = 0,
        limit: int = 500,
    ) -> dict[str, Any] | None:
        with self._lock:
            rec = self._find_locked(recording_id)
            if rec is None:
                return None
            frames = list(rec.frames)
            total = len(frames)
            slice_ = frames[offset : offset + limit]
            return {
                **rec.to_summary(),
                "frame_total": total,
                "offset": offset,
                "limit": limit,
                "frames": [self._frame_dict(f) for f in slice_],
            }

    def export_json(self, recording_id: str) -> dict[str, Any] | None:
        """Full in-memory export for disk write / headless tooling."""
        with self._lock:
            rec = self._find_locked(recording_id)
            if rec is None:
                return None
            return {
                **rec.to_summary(),
                "export_format": "control_toolkit.recording.v1",
                "frames": [self._frame_dict(f) for f in list(rec.frames)],
            }

    def mark_degraded(self, reason: str) -> None:
        with self._lock:
            if self._active is None:
                return
            if self._active.evidence_quality is EvidenceQuality.COMPLETE:
                self._active.evidence_quality = EvidenceQuality.DEGRADED
            self._active.notes.append(reason)

    def _find_locked(self, recording_id: str) -> RecordingSession | None:
        if self._active and self._active.recording_id == recording_id:
            return self._active
        for r in reversed(self._history):
            if r.recording_id == recording_id:
                return r
        return None

    def observe_frame(
        self,
        *,
        bus: str,
        can_id: int,
        dlc: int,
        data: bytes,
        direction: str,
        source: str,
        backend_arrival_ns: int,
        adapter_epoch: int | None,
    ) -> None:
        with self._lock:
            rec = self._active
            if rec is None or rec.state is not RecordingState.RECORDING:
                return
            if len(rec.frames) >= rec.capacity:
                rec.dropped += 1
                rec.evidence_quality = EvidenceQuality.INCOMPLETE
                return
            self._seq += 1
            rec.frames.append(
                RecordedFrame(
                    seq=self._seq,
                    bus=bus,
                    can_id=can_id,
                    dlc=dlc,
                    data_hex=data.hex(),
                    direction=direction,
                    source=source,
                    backend_arrival_ns=backend_arrival_ns,
                    adapter_epoch=adapter_epoch,
                )
            )

    @staticmethod
    def _frame_dict(f: RecordedFrame) -> dict[str, Any]:
        return {
            "seq": f.seq,
            "bus": f.bus,
            "can_id": f.can_id,
            "dlc": f.dlc,
            "data_hex": f.data_hex,
            "direction": f.direction,
            "source": f.source,
            "backend_arrival_ns": f.backend_arrival_ns,
            "adapter_epoch": f.adapter_epoch,
        }
