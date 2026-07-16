import asyncio
import logging
import os
import sys
from typing import Dict, Any, Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import can

from database import CANDatabase
from canalyst_manager import CANalystManager

# Import protocol artifacts
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))
from protocol.codecs.python.codec import encode

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(title="Control UI Backend")

# Enable CORS for frontend development
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Global state
db: Optional[CANDatabase] = None
manager: Optional[CANalystManager] = None

@app.on_event("startup")
async def startup_event():
    global db, manager
    db = CANDatabase(db_path="cui.db")
    await db.connect()
    
    # Initialize CANalystManager with the database instance
    # Assuming channels 0 and 1 (high and low buses)
    manager = CANalystManager(channels=[0, 1], bitrate=500000, db=db, loop=asyncio.get_running_loop())
    
    # Normally we would start it automatically, but we might want a connect endpoint later.
    # For now, we start it.
    manager.start()

@app.on_event("shutdown")
async def shutdown_event():
    if manager:
        manager.stop()
    if db:
        await db.disconnect()

@app.websocket("/api/stream")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            if manager:
                batch = manager.get_ui_batch()
                await websocket.send_json(batch)
            
            # Throttle to ~20Hz (50ms)
            await asyncio.sleep(0.05)
    except WebSocketDisconnect:
        logger.info("WebSocket disconnected")

class InjectRequest(BaseModel):
    message_key: str
    bus: str
    values: Dict[str, Any]

@app.post("/api/inject")
async def inject_command(req: InjectRequest):
    if not manager or not manager.is_connected:
        raise HTTPException(status_code=503, detail="CANalyst is not connected")
        
    # Protocol encoding
    status, frame = encode(req.message_key, req.values, bus=req.bus)
    
    if status != "ok" or not frame:
        raise HTTPException(status_code=400, detail=f"Encode failed: {status}")
        
    # Determine channel from bus (Control-UI convention: 0=high, 1=low)
    # The architecture doc specifies standardizing on this.
    channel = 0 if req.bus == "high" else 1 if req.bus == "low" else None
    
    if channel is None:
        raise HTTPException(status_code=400, detail=f"Unsupported bus for CANalyst: {req.bus}")

    # Build python-can Message
    msg = can.Message(
        arbitration_id=frame.id,
        data=frame.data,
        is_extended_id=(frame.frame_format == "extended"),
        channel=channel
    )
    
    try:
        manager.send(msg)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"TX Error: {str(e)}")
        
    return {"status": "ok", "message_key": req.message_key}

# Run with: uvicorn main:app --reload
