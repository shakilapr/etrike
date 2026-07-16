"""WebSocket stream endpoint (workplan §1.6, §4.3).

Connection sequence:
  1. accept
  2. ``hello`` — protocol wire hash + server monotonic clock (client clock-offset)
  3. initial full ``state`` batch
  4. thereafter, per batch tick (``latest_state_batch_hz``):
       - drain and forward critical ``event``s,
       - send a ``state`` batch only when the store's data version advanced
         (coalescing — never one message per frame),
       - otherwise send a ``heartbeat`` every ``stream_heartbeat_ms``.

Every outbound message carries a monotonic ``batch_seq`` so the client can detect
gaps and request a fresh snapshot with ``{"type": "resync"}``. Only the sender
task writes to the socket; the receiver task only reads (no concurrent sends).
"""

from __future__ import annotations

import asyncio
import json
import time

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from vtc import protocol_bridge as proto

router = APIRouter()


@router.websocket("/stream")
async def stream(ws: WebSocket) -> None:
    await ws.accept()
    config = ws.app.state.config
    lifecycle = ws.app.state.lifecycle
    sub = lifecycle.event_bus.subscribe()
    ctx = {"batch_seq": 0, "last_version": None, "last_hb": 0.0, "force": True}

    await ws.send_json(
        {
            "type": "hello",
            "wire_hash": proto.WIRE_HASH,
            "server_time_ns": time.monotonic_ns(),
        }
    )

    recv = asyncio.create_task(_receiver(ws, ctx))
    send = asyncio.create_task(_sender(ws, lifecycle, sub, config, ctx))
    try:
        await asyncio.wait({recv, send}, return_when=asyncio.FIRST_COMPLETED)
    finally:
        for task in (recv, send):
            if not task.done():
                task.cancel()
        lifecycle.event_bus.unsubscribe(sub)


async def _receiver(ws: WebSocket, ctx: dict) -> None:
    """Read client messages; ``resync`` forces a full state batch next tick."""
    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
            except ValueError:
                msg = {"type": raw}
            if msg.get("type") == "resync":
                ctx["force"] = True
    except WebSocketDisconnect:
        return


async def _sender(ws: WebSocket, lifecycle, sub, config, ctx: dict) -> None:
    batch_interval = 1.0 / max(1, config.latest_state_batch_hz)
    hb_interval = config.stream_heartbeat_ms / 1000
    try:
        while True:
            for event in sub.drain():
                ctx["batch_seq"] += 1
                await ws.send_json(
                    {"type": "event", "batch_seq": ctx["batch_seq"], "event": event}
                )

            snap = lifecycle.latest.snapshot()
            now = time.monotonic()
            if ctx["force"] or snap.version != ctx["last_version"]:
                ctx["batch_seq"] += 1
                await ws.send_json(
                    {
                        "type": "state",
                        "batch_seq": ctx["batch_seq"],
                        "version": snap.version,
                        "sequence": snap.sequence,
                        "messages": [m.model_dump(mode="json") for m in snap.messages],
                    }
                )
                ctx["last_version"] = snap.version
                ctx["force"] = False
                ctx["last_hb"] = now
            elif (now - ctx["last_hb"]) >= hb_interval:
                ctx["batch_seq"] += 1
                await ws.send_json(
                    {
                        "type": "heartbeat",
                        "batch_seq": ctx["batch_seq"],
                        "server_time_ns": time.monotonic_ns(),
                    }
                )
                ctx["last_hb"] = now

            await asyncio.sleep(batch_interval)
    except WebSocketDisconnect:
        return
