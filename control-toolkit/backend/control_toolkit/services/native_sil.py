"""Managed JSON-Lines bridge between virtual CAN and the native RT SIL engine."""

from __future__ import annotations

import json
import queue
import subprocess
import threading
from pathlib import Path
from typing import Callable

from control_toolkit.models.frames import ChannelId, RawFrameEnvelope
from control_toolkit.transport.virtual import VirtualTransportAdapter


class NativeSilBridge:
    """Forward HOST_DRIVE_CMD TX to native RT and inject returned CAN frames."""

    def __init__(
        self,
        executable: str,
        transport: VirtualTransportAdapter,
        on_error: Callable[[str], None] | None = None,
    ) -> None:
        self.executable = Path(executable).expanduser().resolve()
        self.transport = transport
        self.on_error = on_error
        self._process: subprocess.Popen[str] | None = None
        self._commands: queue.Queue[RawFrameEnvelope | None] = queue.Queue(maxsize=256)
        self._writer: threading.Thread | None = None
        self._reader: threading.Thread | None = None
        self._stopping = threading.Event()
        self.last_error: str | None = None

    @property
    def running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    @property
    def pid(self) -> int | None:
        return self._process.pid if self.running and self._process is not None else None

    def start(self) -> None:
        if self.running:
            return
        if not self.executable.is_file():
            raise FileNotFoundError(f"native SIL executable not found: {self.executable}")
        self._stopping.clear()
        self.last_error = None
        self._process = subprocess.Popen(
            [str(self.executable)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self.transport.add_tx_listener(self._on_tx)
        self._writer = threading.Thread(target=self._write_loop, name="native-sil-tx", daemon=True)
        self._reader = threading.Thread(target=self._read_loop, name="native-sil-rx", daemon=True)
        self._writer.start()
        self._reader.start()

    def stop(self) -> None:
        self._stopping.set()
        self.transport.remove_tx_listener(self._on_tx)
        try:
            self._commands.put_nowait(None)
        except queue.Full:
            pass
        process = self._process
        if process is not None:
            if process.stdin is not None:
                try:
                    process.stdin.close()
                except OSError:
                    pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
        for thread in (self._writer, self._reader):
            if thread is not None and thread is not threading.current_thread():
                thread.join(timeout=2)
        self._process = None
        self._writer = None
        self._reader = None

    def _on_tx(self, frame: RawFrameEnvelope) -> None:
        if frame.channel is not ChannelId.HIGH or frame.can_id != 0x300:
            return
        try:
            self._commands.put_nowait(frame)
        except queue.Full:
            self._error("native SIL command queue full")

    def _write_loop(self) -> None:
        while not self._stopping.is_set():
            frame = self._commands.get()
            if frame is None:
                return
            process = self._process
            if process is None or process.stdin is None or process.poll() is not None:
                self._error("native SIL process stopped")
                return
            try:
                process.stdin.write(json.dumps({
                    "type": "frame", "bus": "high", "id": "0x300",
                    "data": list(frame.data),
                }) + "\n")
                process.stdin.write('{"type":"tick","dt_ms":10}\n')
                process.stdin.flush()
            except (BrokenPipeError, OSError) as exc:
                self._error(f"native SIL write failed: {exc}")
                return

    def _read_loop(self) -> None:
        process = self._process
        if process is None or process.stdout is None:
            return
        try:
            for line in process.stdout:
                if self._stopping.is_set():
                    return
                try:
                    item = json.loads(line)
                except json.JSONDecodeError:
                    self._error("native SIL emitted non-JSON stdout")
                    continue
                if item.get("type") != "frame":
                    continue
                bus = item.get("bus")
                can_id = item.get("id")
                data = item.get("data")
                if bus not in ("high", "low") or not isinstance(can_id, str) or not isinstance(data, list):
                    self._error("native SIL emitted malformed frame")
                    continue
                try:
                    self.transport.inject(ChannelId(bus), int(can_id, 0), bytes(data))
                except (ValueError, TypeError, RuntimeError) as exc:
                    self._error(f"native SIL frame rejected: {exc}")
        finally:
            if not self._stopping.is_set():
                self._error("native SIL process exited")

    def _error(self, detail: str) -> None:
        self.last_error = detail
        if self.on_error is not None:
            self.on_error(detail)
