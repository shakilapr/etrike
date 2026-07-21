"""SQLite WAL storage for persistent frame history."""

import asyncio
import logging
from typing import Any

import aiosqlite

from control_toolkit.models.frames import RawFrameEnvelope

logger = logging.getLogger(__name__)

class SqliteStorage:
    def __init__(self, db_path: str = "history.sqlite") -> None:
        self.db_path = db_path
        self._queue: asyncio.Queue[RawFrameEnvelope] = asyncio.Queue()
        self._task: asyncio.Task | None = None
        self._running = False
        self.db: aiosqlite.Connection | None = None

    async def start(self) -> None:
        self._running = True
        self.db = await aiosqlite.connect(self.db_path)
        await self.db.execute("PRAGMA journal_mode=WAL")
        await self.db.execute("PRAGMA synchronous=NORMAL")
        await self.db.execute('''
            CREATE TABLE IF NOT EXISTS frames (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                adapter_epoch INTEGER,
                channel TEXT,
                device_timestamp INTEGER,
                backend_arrival_ns INTEGER,
                can_id INTEGER,
                is_extended BOOLEAN,
                is_remote BOOLEAN,
                dlc INTEGER,
                data_hex TEXT,
                channel_sequence INTEGER,
                global_sequence INTEGER,
                direction TEXT,
                source TEXT
            )
        ''')
        await self.db.execute('''
            CREATE INDEX IF NOT EXISTS idx_frames_arrival ON frames(backend_arrival_ns)
        ''')
        await self.db.commit()
        self._task = asyncio.create_task(self._worker())

    async def stop(self) -> None:
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await asyncio.wait_for(self._task, timeout=1.5)
            except (asyncio.CancelledError, asyncio.TimeoutError):
                pass
            self._task = None
        if self.db:
            try:
                await asyncio.wait_for(self.db.close(), timeout=1.5)
            except (asyncio.TimeoutError, Exception):  # noqa: BLE001
                logger.warning("SQLite close timed out or failed", exc_info=True)
            self.db = None

    def append(self, env: RawFrameEnvelope) -> None:
        """Enqueue a frame for async persistence (thread-safe)."""
        try:
            loop = asyncio.get_running_loop()
            loop.call_soon_threadsafe(self._queue.put_nowait, env)
        except RuntimeError:
            pass  # No running loop, or called from outside asyncio context

    async def _worker(self) -> None:
        batch = []
        try:
            while self._running:
                try:
                    # Wait for at least one item, up to a timeout
                    item = await asyncio.wait_for(self._queue.get(), timeout=0.1)
                    batch.append(item)
                    # Drain the queue as much as possible to write in batches
                    while not self._queue.empty() and len(batch) < 1000:
                        batch.append(self._queue.get_nowait())
                except asyncio.TimeoutError:
                    pass

                if batch and self.db:
                    try:
                        await self.db.executemany('''
                            INSERT INTO frames (
                                adapter_epoch, channel, device_timestamp, backend_arrival_ns,
                                can_id, is_extended, is_remote, dlc, data_hex,
                                channel_sequence, global_sequence, direction, source
                            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        ''', [
                            (
                                e.adapter_epoch, e.channel.value, e.device_timestamp, e.backend_arrival_ns,
                                e.can_id, e.is_extended, e.is_remote, e.dlc, e.data.hex(),
                                e.channel_sequence, e.global_sequence, e.direction.value, e.source.value
                            ) for e in batch
                        ])
                        await self.db.commit()
                    except Exception as e:
                        logger.error(f"Failed to write frames to SQLite: {e}")
                    for _ in range(len(batch)):
                        self._queue.task_done()
                    batch.clear()
        except asyncio.CancelledError:
            pass
