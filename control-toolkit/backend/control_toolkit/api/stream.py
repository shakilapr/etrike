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

Also serialize all ``send_json`` calls: concurrent send from two tasks can race
with close and log::

    Unexpected ASGI message 'websocket.send', after sending 'websocket.close'
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
from typing import Any

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from starlette.websockets import WebSocketState

from control_toolkit import protocol_bridge as proto

router = APIRouter()
log = logging.getLogger("control_toolkit.stream")


@router.websocket("/stream")
async def stream(ws: WebSocket) -> None:
    await ws.accept()
    lifecycle = ws.app.state.lifecycle
    send_lock = asyncio.Lock()

    async def safe_send(payload: dict[str, Any]) -> bool:
        """Send JSON if socket still connected; serialize concurrent senders."""
        if ws.client_state != WebSocketState.CONNECTED:
            return False
        async with send_lock:
            if ws.client_state != WebSocketState.CONNECTED:
                return False
            try:
                await ws.send_json(payload)
                return True
            except (WebSocketDisconnect, RuntimeError) as exc:
                log.debug("stream send stopped: %s", exc)
                return False

    if not await safe_send({"type": "hello", "wire_hash": proto.WIRE_HASH}):
        return

    snap = lifecycle.latest.snapshot()
    if not await safe_send(
        {
            "type": "state",
            "sequence": snap.sequence,
            "wire_hash": snap.wire_hash,
            "messages": [m.model_dump(mode="json") for m in snap.messages],
            "initial": True,
        }
    ):
        return

    sid, queue = await lifecycle.events.subscribe()

    async def pump_events() -> None:
        """Push EventBus payloads to the client (state + heartbeats)."""
        while True:
            event = await queue.get()
            if not await safe_send(event):
                return

    async def pump_client() -> None:
        """
        Sole owner of ``ws.receive`` for this connection.

        One sequential receive loop — never cancelled mid-connection to restart.
        """
        while True:
            message = await ws.receive()
            msg_type = message.get("type")
            if msg_type == "websocket.disconnect":
                return
            if msg_type != "websocket.receive":
                continue
            text = message.get("text")
            if text is not None:
                if not await safe_send({"type": "ack", "echo": text}):
                    return

    out_task = asyncio.create_task(pump_events(), name="ws-stream-out")
    in_task = asyncio.create_task(pump_client(), name="ws-stream-in")
    try:
        done, _pending = await asyncio.wait(
            {out_task, in_task},
            return_when=asyncio.FIRST_COMPLETED,
        )
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
