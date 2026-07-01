#!/usr/bin/env python
import json
import os
import queue
import signal
import sys
import threading
import time
from dataclasses import dataclass

import canalystii


BITRATE = int(os.environ.get("CANALYST_BITRATE", "500000"))
POLL_SECONDS = max(int(os.environ.get("CANALYST_POLL_MS", "5")), 1) / 1000.0
DEVICE_INDEX = int(os.environ.get("CANALYST_DEVICE_INDEX", "0"))
CHANNEL_TO_BUS = {
    0: os.environ.get("CANALYST_CH0_BUS", "low"),
    1: os.environ.get("CANALYST_CH1_BUS", "high"),
}
BUS_TO_CHANNEL = {bus: channel for channel, bus in CHANNEL_TO_BUS.items()}


@dataclass
class PeriodicTask:
    bus: str
    can_id: int
    dlc: int
    data: list[int]
    interval_s: float
    remaining: int | None
    next_due: float


running = True
command_q: queue.Queue[dict] = queue.Queue()
periodic: dict[tuple[str, int], PeriodicTask] = {}
stats = {
    "high": {"total": 0, "tx_total": 0, "by_id": {}, "last_rx": 0.0},
    "low": {"total": 0, "tx_total": 0, "by_id": {}, "last_rx": 0.0},
}


def emit(payload: dict) -> None:
    print(json.dumps(payload, separators=(",", ":")), flush=True)


def parse_can_id(value) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip()
        return int(text, 16) if text.lower().startswith("0x") else int(text, 16)
    raise ValueError(f"invalid CAN id: {value!r}")


def make_message(can_id: int, dlc: int, data: list[int]) -> canalystii.Message:
    padded = [int(byte) & 0xFF for byte in data[:dlc]]
    while len(padded) < 8:
        padded.append(0)
    return canalystii.Message(
        can_id=can_id,
        remote=False,
        extended=can_id > 0x7FF,
        data_len=dlc,
        data=tuple(padded),
    )


def send_frame(dev: canalystii.CanalystDevice, bus: str, can_id: int, dlc: int, data: list[int]) -> None:
    if bus not in BUS_TO_CHANNEL:
        raise ValueError(f"bus {bus!r} is not mapped to a CANalyst-II channel")
    channel = BUS_TO_CHANNEL[bus]
    dev.send(channel, make_message(can_id, dlc, data))
    stats[bus]["tx_total"] += 1


def handle_command(dev: canalystii.CanalystDevice, command: dict) -> None:
    cmd = command.get("cmd")
    request_id = command.get("request_id")

    try:
        if cmd == "send":
            bus = str(command["bus"])
            can_id = parse_can_id(command["id"])
            dlc = int(command["dlc"])
            data = list(command["data"])
            send_frame(dev, bus, can_id, dlc, data)
            emit({"type": "cmd_ack", "request_id": request_id, "status": "ok", "cmd": "send"})
            return

        if cmd == "send_periodic":
            action = command.get("action")
            bus = str(command["bus"])
            can_id = parse_can_id(command["id"])
            key = (bus, can_id)
            if action == "stop":
                periodic.pop(key, None)
                emit({"type": "cmd_ack", "request_id": request_id, "status": "ok", "cmd": "send_periodic", "action": "stop"})
                return

            if action == "start":
                interval_s = max(int(command["interval_ms"]), 1) / 1000.0
                count_value = command.get("count")
                periodic[key] = PeriodicTask(
                    bus=bus,
                    can_id=can_id,
                    dlc=int(command["dlc"]),
                    data=list(command["data"]),
                    interval_s=interval_s,
                    remaining=int(count_value) if count_value is not None else None,
                    next_due=time.monotonic(),
                )
                emit({"type": "cmd_ack", "request_id": request_id, "status": "ok", "cmd": "send_periodic", "action": "start"})
                return

        raise ValueError(f"unsupported command: {cmd!r}")
    except Exception as exc:
        emit({"type": "cmd_ack", "request_id": request_id, "status": "error", "error": str(exc), "cmd": cmd})


def command_reader() -> None:
    while running:
        line = sys.stdin.readline()
        if not line:
            break
        try:
            command_q.put(json.loads(line))
        except Exception as exc:
            emit({"type": "cmd_ack", "status": "error", "error": f"invalid command JSON: {exc}"})


def emit_stats(started_at: float) -> None:
    now = time.time()
    uptime = max(now - started_at, 0.001)
    buses = {}
    for bus, bus_stats in stats.items():
        by_id = bus_stats["by_id"]
        buses[bus] = {
            "active": now - bus_stats["last_rx"] < 5.0,
            "total": bus_stats["total"],
            "fps": bus_stats["total"] / uptime,
            "load_pct": 0,
            "tec": 0,
            "rec": 0,
            "by_id": by_id,
        }
    emit({"type": "stats", "ts": now, "uptime_s": int(uptime), "buses": buses})


def receive_channel(dev: canalystii.CanalystDevice, channel: int) -> None:
    bus = CHANNEL_TO_BUS[channel]
    for message in dev.receive(channel):
        can_id = int(message.can_id)
        can_id_text = f"0x{can_id:03X}"
        dlc = int(message.data_len)
        data = [int(byte) for byte in message.data[:dlc]]
        now = time.time()
        bus_stats = stats[bus]
        bus_stats["total"] += 1
        bus_stats["last_rx"] = now
        bus_stats["by_id"][can_id_text] = bus_stats["by_id"].get(can_id_text, 0) + 1
        emit({"ts": now, "bus": bus, "id": can_id_text, "dlc": dlc, "data": data})


def main() -> int:
    global running

    def stop(_signum, _frame):
        global running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    # Avoid a noisy reset-on-GC path on Windows/WinUSB. Process exit releases the handle.
    try:
        canalystii.device.CanalystDevice.__del__ = lambda self: None
    except Exception:
        pass

    try:
        dev = canalystii.CanalystDevice(device_index=DEVICE_INDEX, bitrate=BITRATE)
    except Exception as exc:
        emit({"type": "status", "adapter_connected": False, "online": False, "error": str(exc)})
        return 2

    emit({
        "type": "status",
        "adapter_connected": True,
        "online": True,
        "adapter": "CANalyst-II",
        "device_index": DEVICE_INDEX,
        "bitrate": BITRATE,
        "channels": CHANNEL_TO_BUS,
    })

    threading.Thread(target=command_reader, daemon=True).start()
    started_at = time.time()
    last_stats = 0.0

    while running:
        for channel in (0, 1):
            receive_channel(dev, channel)

        while True:
            try:
                handle_command(dev, command_q.get_nowait())
            except queue.Empty:
                break

        now_mono = time.monotonic()
        for key, task in list(periodic.items()):
            if now_mono < task.next_due:
                continue
            try:
                send_frame(dev, task.bus, task.can_id, task.dlc, task.data)
            except Exception as exc:
                emit({"type": "cmd_ack", "status": "error", "error": str(exc), "cmd": "send_periodic"})
                periodic.pop(key, None)
                continue
            if task.remaining is not None:
                task.remaining -= 1
                if task.remaining <= 0:
                    periodic.pop(key, None)
                    continue
            task.next_due = now_mono + task.interval_s

        now = time.time()
        if now - last_stats >= 1.0:
            emit_stats(started_at)
            last_stats = now

        time.sleep(POLL_SECONDS)

    emit({"type": "status", "adapter_connected": False, "online": False})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
