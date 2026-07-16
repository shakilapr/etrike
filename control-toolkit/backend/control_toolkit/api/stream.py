"""WebSocket stream endpoint.

Clients receive:
  - hello (wire_hash) on connect
  - initial full state snapshot
  - coalesced latest-state batches and heartbeats via the EventBus
  - acks for client text pings

Per-client queue isolation is owned by EventBus.
"""

from __future__ import annotations

import asyncio
import contextlib

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from control_toolkit import protocol_bridge as proto

router = APIRouter()


@router.websocket("/stream")
async def stream(ws: WebSocket) -> None:
    await ws.accept()
    lifecycle = ws.app.state.lifecycle
    await ws.send_json({"type": "hello", "wire_hash": proto.WIRE_HASH})

    snap = lifecycle.latest.snapshot()
    await ws.send_json(
        {
            "type": "state",
            "sequence": snap.sequence,
            "wire_hash": snap.wire_hash,
            "messages": [m.model_dump(mode="json") for m in snap.messages],
            "initial": True,
        }
    )

    sid, queue = await lifecycle.events.subscribe()
    try:
        while True:
            get_task = asyncio.create_task(queue.get())
            recv_task = asyncio.create_task(ws.receive_text())
            done, pending = await asyncio.wait(
                {get_task, recv_task},
                return_when=asyncio.FIRST_COMPLETED,
            )
            for t in pending:
                t.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await t

            if recv_task in done:
                try:
                    msg = recv_task.result()
                except WebSocketDisconnect:
                    return
                except Exception:
                    # Starlette may raise on disconnect via receive_text.
                    return
                await ws.send_json({"type": "ack", "echo": msg})

            if get_task in done:
                try:
                    event = get_task.result()
                except asyncio.CancelledError:
                    continue
                await ws.send_json(event)
    except WebSocketDisconnect:
        return
    finally:
        await lifecycle.events.unsubscribe(sid)
