#!/usr/bin/env python3
"""Generate per-bus CAN signal tables as Markdown files."""

from pathlib import Path
from can_signals_schema import load_can_database_dir

CAN_DIR = Path(__file__).resolve().parent
OUT_DIR = CAN_DIR.parent.parent / "tem"
HEAD = "| Signal | Byte | Bit | Sz | Type | Factor | Offset | Min | Max | Unit | Notes |\n|--------|------|-----|----|------|--------|--------|-----|-----|------|-------|\n"

def type_str(sig):
    return "Signed" if sig.type and sig.type.value == "signed" else "Unsigned"

def val(v):
    if v is None: return "—"
    if isinstance(v, float) and v == int(v): return str(int(v))
    return str(v)

def clean(text):
    """Strip unicode arrows and dashes that break rendering."""
    if not text: return ""
    return text.replace("→", "->").replace("—", "--").replace("–", "-")

def write_markdown(bus_name, messages, output_path):
    lines = [f"# E-Trike CAN Signal Table — {bus_name.title()} Bus\n"]
    for msg in sorted(messages, key=lambda m: m.id):
        lines.append(f"## 0x{msg.id:03X} — {msg.name}")
        lines.append(f"- **Sender:** {msg.sender}  → **Receivers:** {', '.join(msg.receivers) if msg.receivers else 'All'}")
        cycle = f"{msg.cycle_ms}ms" if msg.cycle_ms else "Event"
        lines.append(f"- **DLC:** {msg.dlc} | **Cycle:** {cycle} | **Bus:** {bus_name}")
        if msg.comment:
            lines.append(f"- {clean(msg.comment)}")
        if not msg.signals:
            lines.append("\n*(No signals — DLC=0 event frame)*\n")
            continue
        lines.append("")
        lines.append(HEAD)
        for sig in msg.signals:
            notes = clean(sig.comment or "")
            if sig.values:
                vals = ", ".join(f"{k}={v}" for k, v in sorted(sig.values.items()))
                notes = (notes + " " + vals).strip()
            lines.append(
                f"| {sig.name} | {sig.byte} | {sig.bit_offset} | {sig.size} | "
                f"{type_str(sig)} | {val(sig.factor)} | {val(sig.offset)} | "
                f"{val(sig.min)} | {val(sig.max)} | {sig.unit or '—'} | {notes} |"
            )
        lines.append("")
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"  Wrote {output_path}")

def main():
    db = load_can_database_dir(CAN_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    high, low = [], []
    seen = set()
    for pname, proto in db.protocols.items():
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
