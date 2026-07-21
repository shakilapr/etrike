"""Managed SYS SIL peer for Computer mode virtual CAN.

In-process peer that encodes protocol SYS frames (heartbeat, safety, diag)
and injects them onto the virtual buses. This is not full SYS firmware —
it provides independently verifiable SYS traffic for topology/freshness
and honest runtime indicators (architecture bugs.md B11 / open SYS SIL work).
"""

from __future__ import annotations

import threading
import time
from typing import Callable

from control_toolkit.models.frames import ChannelId
from control_toolkit.services.encoder import encode_message
from control_toolkit.transport.virtual import VirtualTransportAdapter


class SysSilBridge:
    """Periodic SYS TX onto virtual High/Low via transport.inject (RX path)."""

    SCOPE = (
        "Managed in-process SYS peer: SYS_HEARTBEAT + SYS_SAFETY_STS + SYS_DIAG_RPT "
        "via generated codecs; not full SYS FreeRTOS/firmware tasks"
    )

    def __init__(
        self,
        transport: VirtualTransportAdapter,
        on_error: Callable[[str], None] | None = None,
    ) -> None:
        self.transport = transport
        self.on_error = on_error
        self.last_error: str | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._alive_ctr = 0
        self._started_at: float | None = None

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive() and not self._stop.is_set()

    @property
    def pid(self) -> int | None:
        """In-process peer — no OS pid; expose thread ident for diagnostics."""
        if self.running and self._thread is not None:
            return int(self._thread.ident or 0) or None
        return None

    def start(self) -> None:
        if self.running:
            return
        if not isinstance(self.transport, VirtualTransportAdapter):
            raise RuntimeError("SYS SIL requires virtual transport")
        self._stop.clear()
        self.last_error = None
        self._alive_ctr = 0
        self._started_at = time.monotonic()
        self._thread = threading.Thread(
            target=self._loop,
            name="sys-sil-peer",
            daemon=True,
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        thread = self._thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=2.0)
        self._thread = None
        self._started_at = None

    def _loop(self) -> None:
        # Periods from protocol instances (ms).
        next_hb = 0.0
        next_safety = 0.0
        next_diag = 0.0
        while not self._stop.is_set():
            now = time.monotonic()
            try:
                if now >= next_hb:
                    self._emit_heartbeat()
                    next_hb = now + 0.100
                if now >= next_safety:
                    self._emit_safety()
                    next_safety = now + 0.200
                if now >= next_diag:
                    self._emit_diag()
                    next_diag = now + 1.0
            except Exception as exc:  # pragma: no cover - surfaced as last_error
                self._error(f"SYS SIL emit failed: {exc}")
                return
            self._stop.wait(0.02)

    def _emit_heartbeat(self) -> None:
        self._alive_ctr = (self._alive_ctr + 1) & 0xFF
        values = {
            "alive_ctr": self._alive_ctr,
            "heartbeat_ok": 1,
            "estop_active": 0,
            "mode_auto": 0,
            "can_ok": 1,
            "task_safety_ok": 1,
            "task_brake_ok": 1,
            "task_dispatch_ok": 1,
            "task_can_tx_ok": 1,
        }
        # Protocol: SYS_HEARTBEAT is low-bus only.
        self._inject("sys:sys_heartbeat", "low", values)

    def _emit_safety(self) -> None:
        values = {
            "estop_active": 0,
            "heartbeat_ok": 1,
            "light_left": 0,
            "light_right": 0,
            "light_brake": 0,
            "light_head": 0,
        }
        for bus in ("low", "high"):
            self._inject("sys:sys_safety_sts", bus, values)

    def _emit_diag(self) -> None:
        values = {
            "mode": 0,
            "brake_engaged": 0,
            "brake_fault": 0,
            "heartbeat_ok": 1,
            "rx_overflow": 0,
            "estop_active": 0,
            "free_heap_kb": 128,
            "tec": 0,
            "rec": 0,
        }
        for bus in ("low", "high"):
            self._inject("sys:sys_diag_rpt", bus, values)

    def _inject(self, key: str, bus: str, values: dict) -> None:
        result = encode_message(key=key, bus=bus, values=values)
        if not result.ok:
            self._error(f"SYS SIL encode {key}@{bus}: {result.status}")
            return
        self.transport.inject(
            ChannelId(bus),
            result.can_id,
            result.data,
            is_extended=result.is_extended,
        )

    def _error(self, detail: str) -> None:
        self.last_error = detail
        if self.on_error is not None:
            self.on_error(detail)
