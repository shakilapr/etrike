#!/usr/bin/env python3
"""Generate per-bus CAN signal tables as Markdown from shared/can/*.yaml."""

from pathlib import Path
from can_signals_schema import load_can_database_dir

CAN_DIR = Path(__file__).resolve().parent
OUT_DIR = CAN_DIR.parent.parent / "tem"

HEADER = "| Signal | Byte | Bit | Len | Type | Scale | Unit | Values | Description |\n" \
         "|--------|------|-----|-----|------|-------|------|--------|-------------|\n"

def type_str(sig):
    return "i" if sig.type and sig.type.value == "signed" else "u"

def scale_str(sig):
    """Combine factor and offset into human-readable scale."""
    f, o = sig.factor, sig.offset
    if f == 1.0 and o == 0.0: return "1"
    if o == 0.0: return f"x{f:g}"
    if f == 1.0: return f"+{o:g}"
    return f"x{f:g}+{o:g}"

def clean(text):
    if not text: return ""
    return text.replace("→", "->").replace("—", "--").replace("–", "-")

def write_markdown(bus_name, messages, output_path):
    lines = [f"# E-Trike CAN Signal Table — {bus_name.title()} Bus\n"]
    lines.append(f"> Generated from `shared/can/can_{bus_name}.yaml`. Regenerate: `python generate_can_tables.py`\n")

    for msg in sorted(messages, key=lambda m: m.id):
        cycle = f"{msg.cycle_ms}ms" if msg.cycle_ms else "Event"
        rx = ", ".join(msg.receivers) if msg.receivers else "All"
        lines.append(f"## 0x{msg.id:03X} — {msg.name}")
        lines.append(f"DLC={msg.dlc} | {cycle} | {msg.sender} → {rx}")
        if msg.comment:
            lines.append(f"> {clean(msg.comment)}")

        if not msg.signals:
            lines.append("\n*(DLC=0 event frame — no signals)*\n")
            continue

        lines.append("")
        lines.append(HEADER)
        for sig in msg.signals:
            vals = ""
            if sig.values:
                vals = ", ".join(f"{k}={v}" for k, v in sorted(sig.values.items()))
            desc = clean(sig.comment or "")
            lines.append(
                f"| {sig.name} | {sig.byte} | {sig.bit_offset} | {sig.size} | "
                f"{type_str(sig)} | {scale_str(sig)} | {sig.unit or '-'} | "
                f"{vals} | {desc} |"
            )
        lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"  Wrote {output_path}")

def main():
    db = load_can_database_dir(CAN_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    high, low = [], []
    seen = set()
    for _pname, proto in db.protocols.items():
        bus = proto.bus if proto.bus in ("high", "low") else "low"
        for msg in proto.messages:
            if bus == "high" and msg.id not in seen:
                high.append(msg); seen.add(msg.id)
            elif bus == "low":
                low.append(msg)
    write_markdown("high", high, OUT_DIR / "can_table_high.md")
    write_markdown("low", low, OUT_DIR / "can_table_low.md")
    print(f"Done: high={len(high)} msgs, low={len(low)} msgs")

if __name__ == "__main__":
    main()
