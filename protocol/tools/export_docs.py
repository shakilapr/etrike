#!/usr/bin/env python3
"""
Generate a comprehensive, full Markdown documentation of the E-Trike CAN network.
It extracts everything from the new YAML definitions using protocol.py.
"""

from pathlib import Path
from protocol import load_model, validate_model

CAN_DIR = Path(__file__).resolve().parent
DOCS_DIR = CAN_DIR.parent.parent / "docs"

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

def generate_markdown(filter_type: str, filter_val: str, instances: dict, network: dict, out_path: Path):
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
    
    total_signals = sum(len(message.get("layout", {}).get("fields", [])) for _, message, _ in filtered_instances if message.get("layout", {}).get("kind") == "signals")
    unique_message_ids = {instance["id"] for _, _, instance in filtered_instances}
    
    buses = network.get("buses", [])
    
    lines = [
        f"# CAN Network Documentation — {filter_val} ({filter_type.capitalize()})",
        f"**Description:** Signal reference generated from canonical protocol contracts",
        "",
        "*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*",
        "",
        "## Summary Statistics",
        f"- **Unique CAN Message IDs:** {len(unique_message_ids)}",
        f"- **Total Signal Definitions:** {total_signals}",
        "",
        "---",
        "",
        "## Type Notation",
        "| Notation | Meaning |",
        "|---|---|",
        "| `signed` / `unsigned` | Signed / Unsigned integer |",
        "| `enum` | Enumeration (value map provided) |",
        "| `DLC=0` | Zero-length CAN frame (event signal, no payload) |",
        "",
        "## Message Dictionary"
    ]

    for key, message, instance in filtered_instances:
        frame_id = instance["id"]
        if isinstance(frame_id, str):
            frame_id = int(frame_id, 16 if frame_id.lower().startswith("0x") else 10)
        
        bus_desc = next((b.get("description", "") for b in buses if b["key"] == instance["bus"]), "")
        
        lines.append(f"### 0x{frame_id:03X} — {message['name']} (Bus: {instance['bus']})")
        lines.append(f"- **Sender:** {instance['sender']}")
        lines.append(f"- **Receivers:** {', '.join(instance.get('receivers', [])) if instance.get('receivers') else 'All'}")
        lines.append(f"- **DLC:** {message['dlc']} bytes")
        lines.append(f"- **Cycle:** {instance.get('cycle_ms', 0)} ms (0 = event-based)")
        if message.get("comment"):
            lines.append(f"- **Description:** {message.get('comment')}")
        lines.append("")
        
        layout = message.get("layout", {})
        if layout.get("kind") != "signals" or not layout.get("fields"):
            if message["dlc"] == 0:
                lines.append("*No payload (DLC=0 event frame)*")
            else:
                lines.append(f"*Opaque payload or unsupported layout kind: {layout.get('kind')}*")
            lines.append("")
            continue

        lines.append("| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |")
        lines.append("|---|---|---|---|---|---|---|---|---|")
        
        for field in layout.get("fields", []):
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
            
            lines.append(
                f"| `{sig_name}` | {field['byte']} | {field['bit']} | {field['bits']} | "
                f"{type_str} | {scale_str(factor, offset)} | {range_str} | {field.get('unit', '-')} | {desc} |"
            )
        
        lines.append("")
        
    lines.append("---")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"  [{filter_val:15s}] -> {out_path.relative_to(CAN_DIR.parent.parent)}")

def main():
    model = load_model()
    validated = validate_model(model, check_baseline=False)
    
    network = validated["network"]
    instances = validated["instances"]

    buses = [bus.get("key") for bus in network.get("buses", [])]
    nodes = network.get("nodes", [])

    DOCS_DIR = CAN_DIR.parent.parent / "protocol" / "generated" / "docs"
    bus_dir = DOCS_DIR / "buses"
    node_dir = DOCS_DIR / "nodes"
    
    bus_dir.mkdir(parents=True, exist_ok=True)
    node_dir.mkdir(parents=True, exist_ok=True)

    print("Generating per-bus Markdown docs:")
    for bus in buses:
        out_path = bus_dir / f"{bus}.md"
        generate_markdown("bus", bus, instances, network, out_path)

    print("\nGenerating per-node (ECU) Markdown docs:")
    for node in nodes:
        out_path = node_dir / f"{node.lower()}.md"
        generate_markdown("node", node, instances, network, out_path)

if __name__ == "__main__":
    main()
