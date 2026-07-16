import aiosqlite
import asyncio
import os

class CANDatabase:
    def __init__(self, db_path="cui.db"):
        self.db_path = db_path
        self.connection = None
        self._write_queue = asyncio.Queue()
        self._worker_task = None

    async def connect(self):
        self.connection = await aiosqlite.connect(self.db_path)
        await self._init_schema()
        self._worker_task = asyncio.create_task(self._write_worker())

    async def _init_schema(self):
        await self.connection.execute('''
            CREATE TABLE IF NOT EXISTS frames (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp_ms REAL NOT NULL,
                bus TEXT NOT NULL,
                can_id INTEGER NOT NULL,
                is_error BOOLEAN NOT NULL,
                dlc INTEGER NOT NULL,
                data_hex TEXT NOT NULL,
                decode_status TEXT
            )
        ''')
        await self.connection.commit()

    async def disconnect(self):
        if self._worker_task:
            self._worker_task.cancel()
            try:
                await self._worker_task
            except asyncio.CancelledError:
                pass
            
        # Drain queue if possible
        await self._drain_queue()
            
        if self.connection:
            await self.connection.close()

    async def insert_frame(self, timestamp_ms, bus, can_id, is_error, dlc, data_hex, decode_status=None):
        """Enqueue a frame to be written to the database."""
        await self._write_queue.put({
            "timestamp_ms": timestamp_ms,
            "bus": bus,
            "can_id": can_id,
            "is_error": is_error,
            "dlc": dlc,
            "data_hex": data_hex,
            "decode_status": decode_status
        })

    async def _drain_queue(self):
        """Write all remaining items in the queue."""
        batch = []
        while not self._write_queue.empty():
            batch.append(self._write_queue.get_nowait())
            
        if batch:
            await self._insert_batch(batch)

    async def _insert_batch(self, batch):
        if not self.connection or not batch:
            return
            
        query = '''
            INSERT INTO frames (timestamp_ms, bus, can_id, is_error, dlc, data_hex, decode_status)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        '''
        
        values = [
            (
                f["timestamp_ms"], 
                f["bus"], 
                f["can_id"], 
                f["is_error"], 
                f["dlc"], 
                f["data_hex"], 
                f["decode_status"]
            ) 
            for f in batch
        ]
        
        await self.connection.executemany(query, values)
        await self.connection.commit()

    async def _write_worker(self):
        """Background task that pulls frames from the queue and batches them to SQLite."""
        while True:
            try:
                # Wait for at least one item
                first_item = await self._write_queue.get()
                batch = [first_item]
                
                # Fetch any other immediately available items
                while not self._write_queue.empty() and len(batch) < 100:
                    batch.append(self._write_queue.get_nowait())
                
                await self._insert_batch(batch)
                
                # Yield context to allow other async operations to run
                await asyncio.sleep(0.01)
                
            except asyncio.CancelledError:
                break
            except Exception as e:
                import logging
                logging.error(f"Database write error: {e}")
