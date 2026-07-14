#!/usr/bin/env python3
"""
Generate all E-Trike DBC files from the new YAML contracts using protocol.py parser.
"""

import os
import sys
import re
from io import BytesIO
from pathlib import Path
from typing import Optional

import canmatrix
import canmatrix.formats.dbc
from canmatrix import CanMatrix, Ecu, Frame, Signal, ArbitrationId

# Import the new protocol parser
from protocol import load_model, validate_model

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DBC_DIR = REPO_ROOT / "protocol" / "generated" / "dbc"

def _field_limits(field: dict) -> tuple[float, float]:
    bits = field["bits"]
    if field.get("signed"):
        default_min, default_max = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        default_min, default_max = 0, (1 << bits) - 1
    factor = field.get("factor", 1.0)
    offset = field.get("offset", 0.0)
    return field.get("min", default_min * factor + offset), field.get("max", default_max * factor + offset)

def build_database(filter_type: str, filter_val: str, instances: dict, network: dict) -> CanMatrix:
    db = CanMatrix()
    
    # Add Nodes (ECUs)
    nodes = network.get("nodes", [])
    for node in nodes:
        db.add_ecu(Ecu(node))

    # Add Frames
    for identity, (key, message, instance) in instances.items():
        if filter_type == "bus" and instance["bus"] != filter_val:
            continue
        if filter_type == "node" and instance["sender"] != filter_val and filter_val not in instance.get("receivers", []):
            continue
            
        frame_id = instance["id"]
        if isinstance(frame_id, str):
            frame_id = int(frame_id, 16 if frame_id.lower().startswith("0x") else 10)
            
        f = Frame(
            f"{message['name']}_{instance['bus']}" if filter_type == "node" else message["name"],
            arbitration_id=ArbitrationId(frame_id, extended=(instance.get("frame_format") == "extended")),
            size=message["dlc"],
            transmitters=[instance["sender"]],
            cycle_time=instance.get("cycle_ms", 0),
            comment=message.get("comment", "")
        )
        
        layout = message.get("layout", {})
        if layout.get("kind") == "signals":
            for field in layout.get("fields", []):
                minimum, maximum = _field_limits(field)
                s = Signal(
                    field.get("name", field.get("key")),
                    start_bit=field["byte"] * 8 + field["bit"],
                    size=field["bits"],
                    is_little_endian=(message["byte_order"] == "little"),
                    is_signed=field.get("signed", False),
                    factor=field.get("factor", 1.0),
                    offset=field.get("offset", 0.0),
                    min=minimum,
                    max=maximum,
                    unit=field.get("unit", ""),
                    receivers=instance.get("receivers", []),
                    comment=field.get("comment", "")
                )
                if "enum" in field:
                    s.values = {int(k): str(v) for k, v in field["enum"].items()}
                f.add_signal(s)
        db.add_frame(f)
    return db

def write_dbc(db: CanMatrix, output_path: Path) -> int:
    buf = BytesIO()
    canmatrix.formats.dbc.dump(db, buf)
    text = buf.getvalue().decode("utf-8")
    
    def round_float(m):
        val = float(m.group(0))
        if abs(val) < 1e-10:
            return "0"
        return f"{val:.6g}"
    text = re.sub(r'(?<!\d)(?:\d+\.\d{15,}|0\.\d{10,})(?!\d)', round_float, text)
    
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
    return len(text)

def main():
    model = load_model()
    validated = validate_model(model, check_baseline=False)
    
    network = validated["network"]
    instances = validated["instances"]
    
    buses = [bus.get("key") for bus in network.get("buses", [])]
    nodes = network.get("nodes", [])
    
    total_bytes = 0
    total_frames = 0
    
    print("Generating per-bus DBCs:")
    for bus in buses:
        db = build_database("bus", bus, instances, network)
        if len(db.frames) == 0:
            continue
            
        output_path = DBC_DIR / "buses" / f"{bus}.dbc"
        nbytes = write_dbc(db, output_path)
        total_bytes += nbytes
        total_frames += len(db.frames)
        print(f"  [{bus:15s}] -> {output_path.relative_to(REPO_ROOT)} ({nbytes} bytes, {len(db.frames)} frames)")

    print("\nGenerating per-node (ECU) DBCs:")
    for node in nodes:
        db = build_database("node", node, instances, network)
        if len(db.frames) == 0:
            continue
            
        output_path = DBC_DIR / "nodes" / f"{node.lower()}.dbc"
        nbytes = write_dbc(db, output_path)
        total_bytes += nbytes
        total_frames += len(db.frames)
        print(f"  [{node:15s}] -> {output_path.relative_to(REPO_ROOT)} ({nbytes} bytes, {len(db.frames)} frames)")

if __name__ == "__main__":
    main()
