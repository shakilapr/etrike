"""Sequential message verification (Phase 6 software track).

One active step at a time: stimulus → wait for expected observation →
Pass / Fail / Inconclusive with evidence links. Formal Pass requires
Complete evidence quality when a recording is active.
"""

from __future__ import annotations

import threading
import time
import uuid
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.recording import EvidenceQuality, RecordingService
from control_toolkit.services.session_manager import SessionError
from control_toolkit.services.tx_gate import TxGate
from control_toolkit.state.latest import LatestStore


class TestDisposition(str, Enum):
    PASS = "pass"
    FAIL = "fail"
    INCONCLUSIVE = "inconclusive"
    RUNNING = "running"
    ERROR = "error"
    CANCELED = "canceled"


@dataclass
class TestStepResult:
    test_id: str
    name: str
    disposition: TestDisposition
    started_mono: float
    finished_mono: float | None = None
    detail: str = ""
    stimulus: dict[str, Any] = field(default_factory=dict)
    expect: dict[str, Any] = field(default_factory=dict)
    evidence: dict[str, Any] = field(default_factory=dict)
    observed: dict[str, Any] | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "test_id": self.test_id,
            "name": self.name,
            "disposition": self.disposition.value,
            "started_mono": self.started_mono,
            "finished_mono": self.finished_mono,
            "duration_ms": (
                None
                if self.finished_mono is None
                else round((self.finished_mono - self.started_mono) * 1000, 2)
            ),
            "detail": self.detail,
            "stimulus": self.stimulus,
            "expect": self.expect,
            "evidence": self.evidence,
            "observed": self.observed,
        }


class VerificationService:
    """Run one stimulus/assertion step at a time against virtual latest state."""

    def __init__(
        self,
        *,
        tx_gate: TxGate,
        latest: LatestStore,
        recording: RecordingService,
        require_bench_tx: Callable[[], None],
    ) -> None:
        self._tx_gate = tx_gate
        self._latest = latest
        self._recording = recording
        self._require_bench_tx = require_bench_tx
        self._lock = threading.Lock()
        self._active: TestStepResult | None = None
        self._history: list[TestStepResult] = []
        self._cancel_ids: set[str] = set()

    def get(self, test_id: str) -> dict[str, Any] | None:
        with self._lock:
            if self._active and self._active.test_id == test_id:
                return self._active.to_dict()
            for t in reversed(self._history):
                if t.test_id == test_id:
                    return t.to_dict()
        return None

    def list_tests(self, limit: int = 50) -> list[dict[str, Any]]:
        with self._lock:
            items = list(self._history[-limit:])
            if self._active is not None:
                items.append(self._active)
        return [t.to_dict() for t in reversed(items)]

    def cancel(self, test_id: str) -> dict[str, Any]:
        """Request cancel of a running step; returns current snapshot."""
        with self._lock:
            if self._active is None or self._active.test_id != test_id:
                raise SessionError(
                    "test.not_running",
                    f"test {test_id} is not the active running step",
                    status=404,
                )
            self._cancel_ids.add(test_id)
            return self._active.to_dict()

    def start(
        self,
        *,
        name: str,
        stimulus: dict[str, Any],
        expect: dict[str, Any],
        owner: str = "test:verification",
    ) -> dict[str, Any]:
        """Begin a test on a worker thread; returns immediately while RUNNING."""
        with self._lock:
            if self._active is not None and self._active.disposition is TestDisposition.RUNNING:
                raise SessionError(
                    "test.busy",
                    "another verification step is already running",
                    status=409,
                )
            test_id = f"test_{uuid.uuid4().hex[:12]}"
            result = TestStepResult(
                test_id=test_id,
                name=name or "unnamed",
                disposition=TestDisposition.RUNNING,
                started_mono=time.monotonic(),
                stimulus=dict(stimulus),
                expect=dict(expect),
            )
            self._active = result
            self._cancel_ids.discard(test_id)

        def worker() -> None:
            try:
                self._execute(result, owner=owner)
            except SessionError as exc:
                result.disposition = TestDisposition.ERROR
                result.detail = exc.detail
                result.finished_mono = time.monotonic()
            except Exception as exc:  # noqa: BLE001
                result.disposition = TestDisposition.ERROR
                result.detail = str(exc)
                result.finished_mono = time.monotonic()
            finally:
                with self._lock:
                    if result.finished_mono is None:
                        result.finished_mono = time.monotonic()
                    if result.disposition is TestDisposition.RUNNING:
                        result.disposition = TestDisposition.ERROR
                        result.detail = result.detail or "ended without disposition"
                    self._history.append(result)
                    if self._active is result:
                        self._active = None
                    self._cancel_ids.discard(test_id)

        threading.Thread(
            target=worker, name=f"verify-{test_id}", daemon=True
        ).start()
        return result.to_dict()

    def run(
        self,
        *,
        name: str,
        stimulus: dict[str, Any],
        expect: dict[str, Any],
        owner: str = "test:verification",
    ) -> dict[str, Any]:
        """Synchronous run (blocks until done). Prefer ``start`` + poll for UI."""
        started = self.start(
            name=name, stimulus=stimulus, expect=expect, owner=owner
        )
        test_id = started["test_id"]
        # Poll until terminal (timeout slightly above expect timeout).
        timeout_ms = float(expect.get("timeout_ms") or 500)
        deadline = time.monotonic() + max(2.0, timeout_ms / 1000.0 + 5.0)
        while time.monotonic() < deadline:
            body = self.get(test_id)
            if body is None:
                break
            if body.get("disposition") != TestDisposition.RUNNING.value:
                return body
            time.sleep(0.02)
        body = self.get(test_id)
        if body is None:
            raise SessionError("test.lost", f"test {test_id} disappeared", status=500)
        return body

    def _canceled(self, test_id: str) -> bool:
        with self._lock:
            return test_id in self._cancel_ids

    def _execute(self, result: TestStepResult, *, owner: str) -> None:
        self._require_bench_tx()

        # Evidence gate: formal Pass only with Complete when recording active.
        active_rec = self._recording.active()
        evidence_quality = (
            active_rec.evidence_quality.value if active_rec is not None else "complete"
        )
        result.evidence = {
            "recording_id": active_rec.recording_id if active_rec else None,
            "evidence_quality": evidence_quality,
            "formal": active_rec is not None,
        }
        if active_rec is not None and active_rec.evidence_quality is not EvidenceQuality.COMPLETE:
            result.disposition = TestDisposition.INCONCLUSIVE
            result.detail = (
                f"evidence quality is {active_rec.evidence_quality.value}; "
                "formal Pass requires complete evidence"
            )
            result.finished_mono = time.monotonic()
            return

        stim_type = str(result.stimulus.get("type") or "inject")
        if stim_type != "inject":
            raise SessionError(
                "test.stimulus_unsupported",
                f"unsupported stimulus type: {stim_type}",
                status=400,
            )

        bus = str(result.stimulus["bus"])
        key = str(result.stimulus["key"])
        values = dict(result.stimulus.get("values") or {})

        pre = self._snapshot_message(
            bus=str(result.expect.get("bus") or bus),
            can_id=result.expect.get("can_id"),
            name=result.expect.get("name"),
        )
        result.evidence["pre_step"] = pre

        submit = self._tx_gate.submit(
            bus=bus,
            key=key,
            values=values,
            owner=owner,
            source=FrameSource.INJECTION,
        )
        if submit.disposition == "rejected":
            result.disposition = TestDisposition.FAIL
            result.detail = submit.reason or "stimulus rejected"
            result.finished_mono = time.monotonic()
            return

        result.evidence["stimulus_request_id"] = submit.request_id
        result.evidence["stimulus_lease_id"] = submit.lease_id
        if submit.encode is not None:
            result.evidence["tx"] = {
                "can_id": submit.encode.can_id,
                "data_hex": submit.encode.data.hex(),
                "bus": bus,
            }

        timeout_ms = float(result.expect.get("timeout_ms") or 500)
        deadline = time.monotonic() + timeout_ms / 1000.0
        expect_type = str(result.expect.get("type") or "message_observed")

        observed: dict[str, Any] | None = None
        while time.monotonic() < deadline:
            if self._canceled(result.test_id):
                result.observed = observed
                result.disposition = TestDisposition.CANCELED
                result.detail = "canceled by operator"
                result.finished_mono = time.monotonic()
                return
            observed = self._snapshot_message(
                bus=str(result.expect.get("bus") or bus),
                can_id=result.expect.get("can_id"),
                name=result.expect.get("name"),
            )
            if observed is not None and self._matches(expect_type, result.expect, observed):
                result.observed = observed
                result.disposition = TestDisposition.PASS
                result.detail = "expectation met"
                result.finished_mono = time.monotonic()
                return
            time.sleep(0.01)

        result.observed = observed
        result.disposition = TestDisposition.FAIL
        result.detail = f"timeout after {timeout_ms:.0f} ms waiting for {expect_type}"
        result.finished_mono = time.monotonic()

    def _snapshot_message(
        self,
        *,
        bus: str | None,
        can_id: Any,
        name: Any,
    ) -> dict[str, Any] | None:
        snap = self._latest.snapshot()
        items = list(snap.messages)
        target_id = int(can_id) if can_id is not None else None
        target_name = str(name) if name else None
        for m in items:
            if bus and m.bus != bus:
                continue
            if target_id is not None and int(m.can_id) != target_id:
                continue
            if target_name and m.name != target_name:
                continue
            if target_id is None and target_name is None:
                continue
            return {
                "bus": m.bus,
                "can_id": m.can_id,
                "name": m.name,
                "freshness": getattr(m.freshness, "value", m.freshness),
                "validation_status": getattr(
                    m.validation_status, "value", m.validation_status
                ),
                "signals": {
                    k: {
                        "engineering_value": v.engineering_value,
                        "enum_label": v.enum_label,
                    }
                    for k, v in (m.signals or {}).items()
                },
            }
        return None

    @staticmethod
    def _matches(expect_type: str, expect: dict[str, Any], observed: dict[str, Any]) -> bool:
        if expect_type == "message_observed":
            return observed is not None

        if expect_type == "signal_equals":
            signal = expect.get("signal")
            if not signal:
                return False
            sig = (observed.get("signals") or {}).get(signal)
            if not sig:
                return False
            if "enum" in expect:
                return str(sig.get("enum_label")) == str(expect["enum"])
            if "equals" in expect:
                try:
                    return float(sig.get("engineering_value")) == float(expect["equals"])
                except (TypeError, ValueError):
                    return str(sig.get("engineering_value")) == str(expect["equals"])
            return False

        if expect_type == "signal_in_range":
            signal = expect.get("signal")
            sig = (observed.get("signals") or {}).get(signal or "")
            if not sig:
                return False
            try:
                val = float(sig.get("engineering_value"))
            except (TypeError, ValueError):
                return False
            lo = expect.get("min")
            hi = expect.get("max")
            if lo is not None and val < float(lo):
                return False
            if hi is not None and val > float(hi):
                return False
            return True

        return False
