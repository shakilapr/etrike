#!/usr/bin/env python3
"""Generate per-bus CAN database CSV files from YAML."""

import csv, sys
from pathlib import Path
from can_signals_schema import load_can_database_dir

CAN_DIR = Path(__file__).resolve().parent
OUT_DIR = CAN_DIR.parent.parent / "tem"

HEADER = [
    "Msg Name", "Msg Type", "Msg ID", "Msg Send Type", "Msg Cycle Time (ms)",
    "Msg Length (Byte)", "Bus", "Sender", "Receiver(s)",
    "Signal Name", "Signal Description", "Byte Order", "Start Byte",
    "Start Bit", "Bit Length (Bit)", "Data Type", "Resolution", "Offset",
    "Signal Min.Value (phys)", "Signal Max.Value (phys)", "Initial Value (Hex)",
    "Unit", "Signal Value Description", "Notes"
]

def byte_order_label(bo):
    return "Motorola (big-endian)" if bo.value == "motorola" else "Intel (little-endian)"

def msg_type_label(cycle_ms):
    return "Event" if cycle_ms == 0 else "Cycle"

def write_csv(bus_name, messages, output_path, byte_orders=None):
    rows = []
    for msg in sorted(messages, key=lambda m: m.id):
        base = {
            "Msg Name": msg.name,
            "Msg Type": "Normal",
            "Msg ID": f"0x{msg.id:03X}",
            "Msg Send Type": msg_type_label(msg.cycle_ms),
            "Msg Cycle Time (ms)": msg.cycle_ms,
            "Msg Length (Byte)": msg.dlc,
            "Bus": bus_name.title(),
            "Sender": msg.sender,
            "Receiver(s)": " / ".join(msg.receivers) if msg.receivers else "All",
            "Notes": msg.comment or "",
        }
        if not msg.signals:
            rows.append({**base, "Signal Name": "", "Signal Description": "",
                         "Byte Order": "", "Start Byte": "", "Start Bit": "",
                         "Bit Length (Bit)": "", "Data Type": "",
                         "Resolution": "", "Offset": "", "Signal Min.Value (phys)": "",
                         "Signal Max.Value (phys)": "", "Initial Value (Hex)": "",
                         "Unit": "", "Signal Value Description": ""})
            continue
        for sig in msg.signals:
            row = {**base}
            row["Signal Name"] = sig.name
            row["Signal Description"] = sig.comment or ""
            row["Byte Order"] = byte_order_label(byte_orders.get(msg.id, "motorola"))
            row["Start Byte"] = sig.byte
            row["Start Bit"] = sig.bit_offset
            row["Bit Length (Bit)"] = sig.size
            row["Data Type"] = "Signed" if sig.type and sig.type.value == "signed" else "Unsigned"
            row["Resolution"] = sig.factor if sig.factor != 1.0 else 1
            row["Offset"] = sig.offset if sig.offset != 0.0 else 0
            row["Signal Min.Value (phys)"] = sig.min if sig.min is not None else ""
            row["Signal Max.Value (phys)"] = sig.max if sig.max is not None else ""
            row["Initial Value (Hex)"] = ""
            row["Unit"] = sig.unit or ""
            row["Signal Value Description"] = ""
            row["Notes"] = base["Notes"]
            if sig.values:
                row["Signal Value Description"] = "; ".join(
                    f"{k}={v}" for k, v in sorted(sig.values.items()))
            rows.append(row)
            base = {k: "" for k in base}  # subsequent signals: blank message-level cells

    with open(output_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(HEADER)
        for row in rows:
            w.writerow([row.get(h, "") for h in HEADER])
    print(f"  Wrote {len(rows)} rows to {output_path}")

def main():
    db = load_can_database_dir(CAN_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Collect per-bus messages with deduplication
    high_msgs, low_msgs = [], []
    high_seen, low_seen = set(), set()
    msg_byte_orders = {}  # msg.id -> byte_order
    for pname, proto in db.protocols.items():
        bus = proto.bus if proto.bus in ("high", "low") else "low"
        for msg in proto.messages:
            msg_byte_orders[msg.id] = proto.byte_order  # Enum, not .value
            if bus == "high" and msg.id not in high_seen:
                high_msgs.append(msg)
                high_seen.add(msg.id)
            elif bus == "low" and msg.id not in low_seen:
                low_msgs.append(msg)
                low_seen.add(msg.id)

    bo = msg_byte_orders
    write_csv("high", high_msgs, OUT_DIR / "etrike_can_database_high.csv", byte_orders=bo)
    write_csv("low", low_msgs, OUT_DIR / "etrike_can_database_low.csv", byte_orders=bo)
    write_csv("high/low", list({m.id: m for m in high_msgs + low_msgs}.values()),
              OUT_DIR / "etrike_can_database.csv", byte_orders=bo)

    print(f"\nDone: high={len(high_msgs)} low={len(low_msgs)}")

if __name__ == "__main__":
    main()
