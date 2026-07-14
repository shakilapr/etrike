#!/usr/bin/env python3
"""
Generate a comprehensive, full Markdown documentation of the E-Trike CAN network.
It extracts everything from the YAML definitions: descriptions, ECUs, constants, protocols, and signals.
"""

from pathlib import Path
from can_signals_schema import load_can_database_dir

CAN_DIR = Path(__file__).resolve().parent
DOCS_DIR = CAN_DIR.parent.parent / "docs"


def scale_str(sig):
    f, o = sig.factor, sig.offset
    if f == 1 and o == 0: return "1"
    if o == 0: return f"x{f}"
    if f == 1: return f"+{o}" if o >= 0 else str(o)
    return f"x{f} + {o}"

def main():
    db = load_can_database_dir(CAN_DIR)
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    out_path = DOCS_DIR / "generated_can_documentation.md"

    total_signals = 0
    unique_message_ids = set()
    unique_signal_names = set()

    for pname, proto in db.protocols.items():
        for msg in proto.messages:
            unique_message_ids.add(msg.id)
            total_signals += len(msg.signals)
            for sig in msg.signals:
                unique_signal_names.add(sig.name)

    lines = [
        f"# Full CAN Network Documentation — E-Trike",
        f"**Version:** {db.can_version}",
        f"**Description:** {db.description}",
        "",
        "*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*",
        "",
        "## Summary Statistics",
        f"- **Unique CAN Message IDs:** {len(unique_message_ids)}",
        f"- **Total Signal Definitions:** {total_signals}",
        f"- **Unique Signal Names:** {len(unique_signal_names)}",
        f"- **Protocols/Buses:** {len(db.protocols)}",
        f"- **ECUs Defined:** {len(db.ecus)}",
        "",
        "---",
        ""
    ]

    lines.extend([
        "## CAN Network Topology & Communication Architecture",
        "",
        "The E-Trike utilizes two primary CAN networks running at 500 kbit/s:",
        "1. **High-Level CAN Bus:** Connects the Host (Jetson Orin NX) and the RT (Real-Time Gateway). Used for high-level kinematics, drive commands, and telemetry.",
        "2. **Low-Level CAN Bus:** Connects the RT, SYS (Safety/Body controller), MTR (Motor controller), and Actuators (Steer-by-Wire EPS_C and Brake-by-Wire SEB).",
        "",
        "### Gateway & Forwarding",
        "The **RT** ECU acts as a physical gateway between the High-Level and Low-Level CAN buses. It selectively bridges and forwards key messages:",
        "- **0x001 (`SAFETY_ESTOP`)**: Bridged bidirectionally (any node can trigger).",
        "- **0x011 (`SYS_SAFETY_STS`)**: Forwarded from Low to High.",
        "- **0x120 (`SYS_THROTTLE_STS`)**: Forwarded from Low to High.",
        "- **0x206 (`MTR_MOTOR_FBK`)**: Forwarded from Low to High.",
        "- **0x302 (`HOST_LIGHT_CMD`)**: Forwarded from High to Low.",
        "- **0x600 (`SYS_DIAG_RPT`)**: Forwarded from Low to High.",
        "",
        "### Node Roles & Responsibilities",
        "- **Host:** QM. Transmits Auto-mode drive commands (speed/yaw) and obstacle detection limits.",
        "- **RT:** Gateway and Kinematics. Receives Host commands, calculates kinematics, and issues direct actuator targets (steering/speed) to the Low-Level bus.",
        "- **SYS:** Safety and Body Control. Manages ESTOP states, mode switching, lighting, and overrides brake control during Manual/ESTOP modes.",
        "- **MTR:** Actuation. Drives the physical motor based on CAN inputs and reports feedback.",
        "- **EPS_C & SEB:** Smart steer-by-wire and brake-by-wire actuator modules relying on Intel byte-order sub-protocols.",
        "",
        "---",
        ""
    ])

    lines.extend([
        "## Type Notation",
        "| Notation | Meaning |",
        "|---|---|",
        "| `signed` / `unsigned` | Signed / Unsigned integer |",
        "| `enum` | Enumeration (value map provided) |",
        "| `bitmask` | Bitfield, each bit is a flag |",
        "| `DLC=0` | Zero-length CAN frame (event signal, no payload) |",
        "",
        "## Network Nodes (ECUs)",
        "| Node Name | Description |",
        "|---|---|"
    ])

    for ecu in db.ecus:
        lines.append(f"| **{ecu.name}** | {ecu.comment} |")

    lines.extend([
        "",
        "---",
        "",
        "## Global Constants & Parameters",
        "| Parameter | Value |",
        "|---|---|"
    ])

    if db.constants:
        for k, v in db.constants.items():
            lines.append(f"| `{k}` | `{v}` |")
    else:
        lines.append("| *(None defined)* | - |")

    lines.extend([
        "",
        "---",
        "",
        "## Message Dictionary"
    ])

    for pname, proto in db.protocols.items():
        lines.append(f"### Protocol: `{pname}`")
        lines.append(f"**Physical Bus:** {proto.bus} | **Byte Order:** {proto.byte_order.value}")
        lines.append("")

        for msg in proto.messages:
            lines.append(f"#### 0x{msg.id:03X} — {msg.name}")
            lines.append(f"- **Sender:** {msg.sender}")
            lines.append(f"- **Receivers:** {', '.join(msg.receivers) if msg.receivers else 'All'}")
            lines.append(f"- **DLC:** {msg.dlc} bytes")
            lines.append(f"- **Cycle:** {msg.cycle_ms} ms (0 = event-based)")
            if msg.comment:
                lines.append(f"- **Description:** {msg.comment}")
            lines.append("")

            if not msg.signals:
                lines.append("*No payload (DLC=0 event frame)*")
                lines.append("")
                continue

            lines.append("| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |")
            lines.append("|---|---|---|---|---|---|---|---|---|")
            
            for sig in msg.signals:
                type_str = "signed" if sig.type == "signed" else "unsigned"
                range_str = f"[{sig.min:g}, {sig.max:g}]"
                
                desc = sig.comment.replace('\n', ' ') if sig.comment else ""
                if sig.values:
                    vals = ", ".join(f"{k}={v}" for k, v in sig.values.items())
                    desc += f" (Values: {vals})"
                
                lines.append(
                    f"| `{sig.name}` | {sig.byte} | {sig.bit_offset} | {sig.size} | "
                    f"{type_str} | {scale_str(sig)} | {range_str} | {sig.unit or '-'} | {desc} |"
                )
            
            lines.append("")
        
        lines.append("---")
        lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Successfully generated {out_path} (Full Documentation)")

if __name__ == "__main__":
    main()
