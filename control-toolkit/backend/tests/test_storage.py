import asyncio
import os
import tempfile

import aiosqlite
import pytest
from pydantic import ValidationError

from control_toolkit.models.frames import ChannelId, Direction, FrameSource, RawFrameEnvelope
from control_toolkit.state.storage import SqliteStorage

@pytest.fixture
def temp_db_path():
    fd, path = tempfile.mkstemp(suffix=".sqlite")
    os.close(fd)
    yield path
    try:
        os.remove(path)
    except OSError:
        pass

@pytest.mark.asyncio
async def test_sqlite_storage_initialization(temp_db_path):
    storage = SqliteStorage(db_path=temp_db_path)
    await storage.start()
    
    # Check tables are created
    assert storage.db is not None
    async with storage.db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='frames'") as cursor:
        assert await cursor.fetchone() is not None
        
    await storage.stop()

@pytest.mark.asyncio
async def test_sqlite_storage_append_and_flush(temp_db_path):
    storage = SqliteStorage(db_path=temp_db_path)
    await storage.start()
    
    env = RawFrameEnvelope(
        adapter_epoch=1,
        channel=ChannelId.HIGH,
        device_timestamp=1000,
        backend_arrival_ns=2000,
        can_id=0x123,
        dlc=8,
        data=b"\x00\x11\x22\x33\x44\x55\x66\x77",
        channel_sequence=1,
        global_sequence=1,
        direction=Direction.RX,
        source=FrameSource.PHYSICAL,
    )
    
    # Needs to be called in an event loop
    storage.append(env)
    
    # Wait briefly for worker to flush
    await asyncio.sleep(0.2)
    
    async with storage.db.execute("SELECT COUNT(*) FROM frames") as cursor:
        count = await cursor.fetchone()
        assert count[0] == 1
        
    async with storage.db.execute("SELECT can_id, data_hex, channel, source FROM frames") as cursor:
        row = await cursor.fetchone()
        assert row[0] == 0x123
        assert row[1] == "0011223344556677"
        assert row[2] == "high"
        assert row[3] == "physical"
        
    await storage.stop()
