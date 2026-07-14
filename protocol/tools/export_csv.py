#!/usr/bin/env python3
"""
Generate CSV tables of the E-Trike CAN network from the YAML contracts using protocol.py parser.
"""

import csv
import sys
from pathlib import Path

# Import the new protocol parser
from protocol import load_model, validate_model

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CSV_DIR = REPO_ROOT / "protocol" / "generated" / "csv"

def scale_str(factor, offset):
    if factor == 1.0 and offset == 0.0: return "1"
    if offset == 0.0: return f"x{factor:g}"
    if factor == 1.0: return f"+{offset:g}" if offset >= 0 else f"{offset:g}"
    return f"x{factor:g} + {offset:g}"

def _field_limits(field: dict) -> tuple[float, float]:
    bits = field["bits"]
    if field.get("signed"):
        default_min, default_max = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        default_min, default_max = 0, (1 << bits) - 1
    factor = field.get("factor", 1.0)
    offset = field.get("offset", 0.0)
    return field.get("min", default_min * factor + offset), field.get("max", default_max * factor + offset)

def export_csv(filter_type: str, filter_val: str, instances: dict, out_dir: Path):
    filtered_instances = []
    for identity, (key, message, instance) in instances.items():
        if filter_type == "bus" and instance["bus"] != filter_val:
            continue
        if filter_type == "node" and instance["sender"] != filter_val and filter_val not in instance.get("receivers", []):
            continue
        filtered_instances.append((key, message, instance))
        
    if not filtered_instances:
        return
        
    filtered_instances.sort(key=lambda x: (x[2]["id"] if isinstance(x[2]["id"], int) else int(x[2]["id"], 16)))
    
    csv_path = out_dir / f"{filter_val.lower()}.csv"
    
    with open(csv_path, mode="w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            "Message ID (Hex)", "Message Name", "Sender", "Receivers", "DLC", "Cycle (ms)",
            "Signal Name", "Byte", "Bit", "Size", "Type", "Scale", "Range", "Unit", "Description"
        ])
        
        for key, message, instance in filtered_instances:
            frame_id = instance["id"]
            if isinstance(frame_id, str):
                frame_id = int(frame_id, 16 if frame_id.lower().startswith("0x") else 10)
            
            msg_id_hex = f"0x{frame_id:03X}"
            # If generating node, append the bus to disambiguate the frame
            msg_name = f"{message['name']}_{instance['bus']}" if filter_type == "node" else message["name"]
            sender = instance["sender"]
            receivers = ", ".join(instance.get("receivers", [])) if instance.get("receivers") else "All"
            dlc = message["dlc"]
            cycle = instance.get("cycle_ms", 0)
            
            layout = message.get("layout", {})
            if layout.get("kind") != "signals" or not layout.get("fields"):
                writer.writerow([
                    msg_id_hex, msg_name, sender, receivers, dlc, cycle,
                    "(No signals/Opaque)", "", "", "", "", "", "", "", message.get("comment", "").replace("\n", " ")
                ])
                continue
                
            for i, field in enumerate(layout.get("fields", [])):
                type_str = "signed" if field.get("signed") else "unsigned"
                minimum, maximum = _field_limits(field)
                range_str = f"[{minimum:g}, {maximum:g}]"
                
                desc = field.get("comment", "").replace('\n', ' ')
                if "enum" in field:
                    vals = ", ".join(f"{k}={v}" for k, v in field["enum"].items())
                    desc += f" (Values: {vals})"
                
                sig_name = field.get("name", field.get("key"))
                factor = field.get("factor", 1.0)
                offset = field.get("offset", 0.0)
                
                # Only write message level info on first row
                if i == 0:
                    writer.writerow([
                        msg_id_hex, msg_name, sender, receivers, dlc, cycle,
                        sig_name, field["byte"], field["bit"], field["bits"],
                        type_str, scale_str(factor, offset), range_str, field.get("unit", ""), desc
                    ])
                else:
                    writer.writerow([
                        "", "", "", "", "", "",
                        sig_name, field["byte"], field["bit"], field["bits"],
                        type_str, scale_str(factor, offset), range_str, field.get("unit", ""), desc
                    ])
                    
    print(f"  [{filter_val:15s}] -> {csv_path.relative_to(REPO_ROOT)}")

def main():
    model = load_model()
    validated = validate_model(model, check_baseline=False)
    
    network = validated["network"]
    instances = validated["instances"]
    
    buses = [bus.get("key") for bus in network.get("buses", [])]
    nodes = network.get("nodes", [])
    
    bus_dir = CSV_DIR / "buses"
    node_dir = CSV_DIR / "nodes"
    
    bus_dir.mkdir(parents=True, exist_ok=True)
    node_dir.mkdir(parents=True, exist_ok=True)
    
    print("Generating per-bus CSVs:")
    for bus in buses:
        export_csv("bus", bus, instances, bus_dir)
        
    print("\nGenerating per-node (ECU) CSVs:")
    for node in nodes:
        export_csv("node", node, instances, node_dir)

if __name__ == "__main__":
    main()
