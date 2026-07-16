"""WebSocket stream endpoint.

Clients receive:
  - hello (wire_hash) on connect
  - initial full state snapshot
  - coalesced latest-state batches and heartbeats via the EventBus
  - acks for client text pings

Per-client queue isolation is owned by EventBus.

Important: never cancel a pending ``ws.receive*()`` task and immediately start
another receive on the same socket. Uvicorn/Starlette keep the ASGI receive
future alive after Task.cancel(), which raises::

    RuntimeError: Concurrent call to receive() is not allowed

and tears down the handler — frontend Offline/Lost reconnect thrash.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from control_toolkit import protocol_bridge as proto

router = APIRouter()
log = logging.getLogger("control_toolkit.stream")


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

    async def pump_events() -> None:
        """Push EventBus payloads to the client (state + heartbeats)."""
        while True:
            event = await queue.get()
            await ws.send_json(event)

    async def pump_client() -> None:
        """
        Sole owner of ``ws.receive`` for this connection.

        One sequential receive loop — never cancelled mid-connection to restart.
        """
        while True:
            # Starlette receive dict form avoids receive_text edge cases.
            message = await ws.receive()
            msg_type = message.get("type")
            if msg_type == "websocket.disconnect":
                return
            if msg_type != "websocket.receive":
                continue
            text = message.get("text")
            if text is not None:
                await ws.send_json({"type": "ack", "echo": text})
            # Binary client frames ignored (not used by control-toolkit UI).

    out_task = asyncio.create_task(pump_events(), name="ws-stream-out")
    in_task = asyncio.create_task(pump_client(), name="ws-stream-in")
    try:
        done, _pending = await asyncio.wait(
            {out_task, in_task},
            return_when=asyncio.FIRST_COMPLETED,
        )
        # Surface the first failure (if any) for logs; disconnect is normal.
        for task in done:
            with contextlib.suppress(asyncio.CancelledError, WebSocketDisconnect):
                exc = task.exception()
                if exc is not None and not isinstance(
                    exc, (WebSocketDisconnect, asyncio.CancelledError)
                ):
                    log.warning("stream task ended: %s", exc)
    except WebSocketDisconnect:
        return
    finally:
        out_task.cancel()
        in_task.cancel()
        with contextlib.suppress(asyncio.CancelledError, Exception):
            await asyncio.gather(out_task, in_task, return_exceptions=True)
        await lifecycle.events.unsubscribe(sid)
