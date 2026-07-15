"""WebSocket stream endpoint (workplan §1.6, §4.3).

Phase 1 handshake only: accept, exchange protocol hash, then hold the connection
with the stream heartbeat. Coalesced latest-state batches and critical-event
fan-out are added with the event bus (§1.6 onward).
"""

from __future__ import annotations

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from vtc import protocol_bridge as proto

router = APIRouter()


@router.websocket("/stream")
async def stream(ws: WebSocket) -> None:
    await ws.accept()
    await ws.send_json({"type": "hello", "wire_hash": proto.WIRE_HASH})
    try:
        while True:
            # Phase 1: echo client pings; real subscription protocol lands in §1.6.
            msg = await ws.receive_text()
            await ws.send_json({"type": "ack", "echo": msg})
    except WebSocketDisconnect:
        return
