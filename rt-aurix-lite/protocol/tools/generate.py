#!/usr/bin/env python3
"""Validate, generate, and inspect the RT-AURIX-Lite (RT-only) protocol subset.

This is a PARALLEL tool for the stripped contracts under rt-aurix-lite/protocol/.
It reuses the shared protocol logic (protocol.tools.protocol) without modifying it,
pointing it at this subset's root so the canonical RT/SYS protocol tooling is untouched.

Usage (from the repository root):
    python rt-aurix-lite/protocol/tools/generate.py validate
    python rt-aurix-lite/protocol/tools/generate.py generate
    python rt-aurix-lite/protocol/tools/generate.py generate --check
    python rt-aurix-lite/protocol/tools/generate.py inspect 0x204
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]  # etrike/
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

# This subset's protocol package root (contains contracts/, vectors/, generated/).
SUBSET_ROOT = Path(__file__).resolve().parents[1]  # rt-aurix-lite/protocol/

# Reuse the unmodified shared logic. None of the shared tool files are edited;
# they are imported and pointed at this subset's root.
from protocol.tools.protocol import (  # noqa: E402
    ContractError,
    hashes,
    load_model,
    render_outputs,
    validate_model,
)


def write_or_check(outputs: dict[str, str], *, check: bool) -> list[str]:
    """Write rendered outputs under this subset's generated/ dir (parallel to shared write_or_check)."""
    base = SUBSET_ROOT / "generated"
    changed = []
    for relative, content in sorted(outputs.items()):
        path = base / relative
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != content:
            changed.append(relative)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="\n")
    return changed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate, generate, and inspect the RT-AURIX-Lite protocol subset.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate", help="validate the subset contracts")
    generate = subparsers.add_parser("generate", help="generate subset codec/manifest artifacts")
    generate.add_argument("--check", action="store_true", help="read-only verification; fail if output differs")
    subparsers.add_parser("derive", help="generate derivative DBC/CSV/docs artifacts under generated/")
    inspect = subparsers.add_parser("inspect", help="inspect a message by ID")
    inspect.add_argument("id")
    inspect.add_argument("--bus", default=None)
    return parser


def derive() -> None:
    """Generate derivative DBC/CSV/docs artifacts for the subset, fully self-contained.

    Imports only the shared load_model/validate_model logic (clean, with the repo root on
    sys.path) and the standalone canmatrix library. It does NOT import the shared export
    scripts, keeping this subset tool parallel and independent of protocol/tools changes.
    """
    import csv
    import io
    import re

    import canmatrix
    import canmatrix.formats.dbc
    from canmatrix import ArbitrationId, CanMatrix, Ecu, Frame, Signal

    def field_limits(field: dict) -> tuple[float, float]:
        bits = field["bits"]
        if field.get("signed"):
            dmin, dmax = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
        else:
            dmin, dmax = 0, (1 << bits) - 1
        factor, offset = field.get("factor", 1.0), field.get("offset", 0.0)
        return field.get("min", dmin * factor + offset), field.get("max", dmax * factor + offset)

    def scale_str(factor: float, offset: float) -> str:
        if factor == 1.0 and offset == 0.0:
            return "1"
        if offset == 0.0:
            return f"x{factor:g}"
        if factor == 1.0:
            return f"+{offset:g}" if offset >= 0 else f"{offset:g}"
        return f"x{factor:g} + {offset:g}"

    def build_database(filter_type: str, filter_val: str) -> CanMatrix:
        db = CanMatrix()
        for node in network.get("nodes", []):
            db.add_ecu(Ecu(node))
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
                comment=message.get("comment", ""),
            )
            layout = message.get("layout", {})
            if layout.get("kind") == "signals":
                for field in layout.get("fields", []):
                    minimum, maximum = field_limits(field)
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
                        comment=field.get("comment", ""),
                    )
                    if "enum" in field:
                        s.values = {int(k): str(v) for k, v in field["enum"].items()}
                    f.add_signal(s)
            db.add_frame(f)
        return db

    def write_dbc(db: CanMatrix, output_path: Path) -> int:
        buf = io.BytesIO()
        canmatrix.formats.dbc.dump(db, buf)
        text = buf.getvalue().decode("utf-8")

        def round_float(m):
            val = float(m.group(0))
            if abs(val) < 1e-10:
                return "0"
            return f"{val:.6g}"

        text = re.sub(r"(?<!\d)(?:\d+\.\d{15,}|0\.\d{10,})(?!\d)", round_float, text)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(text, encoding="utf-8")
        return len(text)

    def export_csv(filter_type: str, filter_val: str, out_dir: Path) -> None:
        rows = []
        for identity, (key, message, instance) in instances.items():
            if filter_type == "bus" and instance["bus"] != filter_val:
                continue
            if filter_type == "node" and instance["sender"] != filter_val and filter_val not in instance.get("receivers", []):
                continue
            rows.append((key, message, instance))
        if not rows:
            return
        rows.sort(key=lambda x: (x[2]["id"] if isinstance(x[2]["id"], int) else int(x[2]["id"], 16)))
        path = out_dir / f"{filter_val.lower()}.csv"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.writer(fh)
            writer.writerow(["Message ID (Hex)", "Message Name", "Sender", "Receivers", "DLC", "Cycle (ms)",
                             "Signal Name", "Byte", "Bit", "Size", "Type", "Scale", "Range", "Unit", "Description"])
            for key, message, instance in rows:
                frame_id = instance["id"]
                if isinstance(frame_id, str):
                    frame_id = int(frame_id, 16 if frame_id.lower().startswith("0x") else 10)
                msg_id_hex = f"0x{frame_id:03X}"
                msg_name = f"{message['name']}_{instance['bus']}" if filter_type == "node" else message["name"]
                sender = instance["sender"]
                receivers = ", ".join(instance.get("receivers", [])) if instance.get("receivers") else "All"
                dlc = message["dlc"]
                cycle = instance.get("cycle_ms", 0)
                layout = message.get("layout", {})
                fields = layout.get("fields", []) if layout.get("kind") == "signals" else []
                if not fields:
                    writer.writerow([msg_id_hex, msg_name, sender, receivers, dlc, cycle,
                                     "(No signals/Opaque)", "", "", "", "", "", "", "", message.get("comment", "").replace("\n", " ")])
                    continue
                for i, field in enumerate(fields):
                    type_str = "signed" if field.get("signed") else "unsigned"
                    minimum, maximum = field_limits(field)
                    range_str = f"[{minimum:g}, {maximum:g}]"
                    desc = field.get("comment", "").replace("\n", " ")
                    if "enum" in field:
                        vals = ", ".join(f"{k}={v}" for k, v in field["enum"].items())
                        desc += f" (Values: {vals})"
                    sig_name = field.get("name", field.get("key"))
                    factor = field.get("factor", 1.0)
                    offset = field.get("offset", 0.0)
                    if i == 0:
                        writer.writerow([msg_id_hex, msg_name, sender, receivers, dlc, cycle,
                                         sig_name, field["byte"], field["bit"], field["bits"], type_str,
                                         scale_str(factor, offset), range_str, field.get("unit", ""), desc])
                    else:
                        writer.writerow(["", "", "", "", "", "", sig_name, field["byte"], field["bit"], field["bits"],
                                         type_str, scale_str(factor, offset), range_str, field.get("unit", ""), desc])

    def generate_markdown(filter_type: str, filter_val: str, out_path: Path) -> None:
        rows = []
        for identity, (key, message, instance) in instances.items():
            if filter_type == "bus" and instance["bus"] != filter_val:
                continue
            if filter_type == "node" and instance["sender"] != filter_val and filter_val not in instance.get("receivers", []):
                continue
            rows.append((key, message, instance))
        if not rows:
            return
        rows.sort(key=lambda x: (x[2]["id"] if isinstance(x[2]["id"], int) else int(x[2]["id"], 16)))
        total_signals = sum(len(message.get("layout", {}).get("fields", []))
                            for _, message, _ in rows if message.get("layout", {}).get("kind") == "signals")
        unique_ids = {instance["id"] for _, _, instance in rows}
        lines = [
            f"# CAN Network Documentation — {filter_val} ({filter_type.capitalize()})",
            "**Description:** Signal reference generated from the RT-AURIX-Lite protocol subset",
            "",
            "*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*",
            "",
            "## Summary Statistics",
            f"- **Unique CAN Message IDs:** {len(unique_ids)}",
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
            "## Message Dictionary",
        ]
        for key, message, instance in rows:
            frame_id = instance["id"]
            if isinstance(frame_id, str):
                frame_id = int(frame_id, 16 if frame_id.lower().startswith("0x") else 10)
            lines.append(f"### 0x{frame_id:03X} — {message['name']} (Bus: {instance['bus']})")
            lines.append(f"- **Sender:** {instance['sender']}")
            lines.append(f"- **Receivers:** {', '.join(instance.get('receivers', [])) if instance.get('receivers') else 'All'}")
            lines.append(f"- **DLC:** {message['dlc']} bytes")
            lines.append(f"- **Cycle:** {instance.get('cycle_ms', 0)} ms (0 = event-based)")
            if message.get("comment"):
                lines.append(f"- **Description:** {message.get('comment')}")
            lines.append("")
            layout = message.get("layout", {})
            fields = layout.get("fields", []) if layout.get("kind") == "signals" else []
            if message["dlc"] == 0:
                lines.append("*No payload (DLC=0 event frame)*")
                lines.append("")
                continue
            if not fields:
                lines.append(f"*Opaque payload or unsupported layout kind: {layout.get('kind')}*")
                lines.append("")
                continue
            lines.append("| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |")
            lines.append("|---|---|---|---|---|---|---|---|---|")
            for field in fields:
                type_str = "signed" if field.get("signed") else "unsigned"
                minimum, maximum = field_limits(field)
                range_str = f"[{minimum:g}, {maximum:g}]"
                desc = field.get("comment", "").replace("\n", " ")
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
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n".join(lines), encoding="utf-8")

    model = load_model(SUBSET_ROOT)
    validated = validate_model(model, check_baseline=False)
    network = validated["network"]
    instances = validated["instances"]
    buses = [bus.get("key") for bus in network.get("buses", [])]
    nodes = network.get("nodes", [])

    out_root = SUBSET_ROOT / "generated"

    # DBC
    dbc_dir = out_root / "dbc"
    for bus in buses:
        db = build_database("bus", bus)
        if not db.frames:
            continue
        path = dbc_dir / "buses" / f"{bus}.dbc"
        nbytes = write_dbc(db, path)
        print(f"  [dbc:{bus:15s}] -> {path} ({nbytes} bytes, {len(db.frames)} frames)")
    for node in nodes:
        db = build_database("node", node)
        if not db.frames:
            continue
        path = dbc_dir / "nodes" / f"{node.lower()}.dbc"
        nbytes = write_dbc(db, path)
        print(f"  [dbc:{node:15s}] -> {path} ({nbytes} bytes, {len(db.frames)} frames)")

    # CSV
    csv_dir = out_root / "csv"
    for bus in buses:
        export_csv("bus", bus, csv_dir / "buses")
    for node in nodes:
        export_csv("node", node, csv_dir / "nodes")
    print("  [csv] generated")

    # Markdown docs
    doc_dir = out_root / "docs"
    for bus in buses:
        generate_markdown("bus", bus, doc_dir / "buses" / f"{bus}.md")
    for node in nodes:
        generate_markdown("node", node, doc_dir / "nodes" / f"{node.lower()}.md")
    print("  [docs] generated")


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        model = load_model(SUBSET_ROOT)
        # Subset has no frozen baseline manifest; skip baseline validation.
        validated = validate_model(model, check_baseline=False)
        if args.command == "validate":
            wire_hash, network_hash = hashes(model, validated)
            print(
                f"valid: {len(validated['messages'])} messages, "
                f"{len(validated['instances'])} instances, "
                f"SEMANTIC_HASH={wire_hash}, NETWORK_HASH={network_hash}"
            )
        elif args.command == "generate":
            semantic_hash, network_hash = hashes(model, validated)
            outputs = render_outputs(model, validated)
            changed = write_or_check(outputs, check=args.check)
            if args.check and changed:
                raise ContractError("generated output differs: " + ", ".join(changed))
            print("generated output is current" if not changed else "generated: " + ", ".join(changed))
            print(f"SEMANTIC_HASH={semantic_hash}")
            print(f"NETWORK_HASH={network_hash}")
        elif args.command == "derive":
            derive()
        else:
            from protocol.tools.protocol import inspect_message

            import json as _json

            print(_json.dumps(inspect_message(model, validated, args.id, args.bus), indent=2, sort_keys=True), end="\n")
    except ContractError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
