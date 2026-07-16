import pytest
import aiosqlite
import os
import sys
import asyncio

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from database import CANDatabase

import pytest_asyncio

@pytest_asyncio.fixture
async def temp_db():
    db_path = "test_cui.db"
    if os.path.exists(db_path):
        os.remove(db_path)
        
    db = CANDatabase(db_path)
    await db.connect()
    yield db
    await db.disconnect()
    
    if os.path.exists(db_path):
        os.remove(db_path)

@pytest.mark.asyncio
async def test_database_insert_and_retrieve(temp_db):
    db = temp_db
    
    # Insert some frames
    await db.insert_frame(
        timestamp_ms=100.5,
        bus="high",
        can_id=0x100,
        is_error=False,
        dlc=2,
        data_hex="aabb",
        decode_status="OK"
    )
    
    # Give the background worker a moment to process the queue
    await asyncio.sleep(0.1)
    
    # Drain queue completely just to be sure
    await db._drain_queue()
    
    # Query database to verify
    async with aiosqlite.connect(db.db_path) as conn:
        conn.row_factory = aiosqlite.Row
        cursor = await conn.execute("SELECT * FROM frames")
        rows = await cursor.fetchall()
        
    assert len(rows) == 1
    assert rows[0]["bus"] == "high"
    assert rows[0]["can_id"] == 0x100
    assert rows[0]["data_hex"] == "aabb"
    assert rows[0]["is_error"] == False
