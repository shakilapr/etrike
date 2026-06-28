#!/usr/bin/env python3
"""Generate per-bus CAN signal tables as clean HTML from shared/can/*.yaml."""

from pathlib import Path
from can_signals_schema import load_can_database_dir

CAN_DIR = Path(__file__).resolve().parent
OUT_DIR = CAN_DIR.parent.parent / "tem"

CSS = """<style>
body{font:14px system-ui,sans-serif;max-width:1200px;margin:0 auto;padding:20px;color:#222}
h1{border-bottom:2px solid #333;padding-bottom:8px}
h2{background:#f0f0f0;padding:8px 12px;margin:24px 0 8px;border-radius:4px;font-size:16px}
.meta{color:#666;font-size:13px;margin:4px 0 8px 12px}
.comment{color:#888;font-style:italic;margin:0 0 8px 12px;font-size:13px}
table{width:100%;border-collapse:collapse;margin:8px 0 20px;font-size:13px}
th{background:#333;color:#fff;padding:6px 8px;text-align:left;font-weight:600}
td{padding:5px 8px;border-bottom:1px solid #ddd}
tr:hover{background:#f5f5f5}
.no-signals{color:#999;margin:8px 12px;font-size:13px}
</style>"""

def type_str(sig):
    return "signed" if sig.type and sig.type.value == "signed" else "unsigned"

def scale_str(sig):
    f, o = sig.factor, sig.offset
    if f == 1.0 and o == 0.0: return "1"
    if o == 0.0: return f"&times;{f:g}"
    return f"&times;{f:g} + {o:g}"

def clean(text):
    if not text: return ""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def write_html(bus_name, messages, output_path):
    lines = [f"<!DOCTYPE html><html><head><meta charset=utf-8><title>E-Trike CAN — {bus_name.title()} Bus</title>{CSS}</head><body>"]
    lines.append(f"<h1>E-Trike CAN Signal Table — {bus_name.title()} Bus</h1>")
    lines.append(f"<p class=meta>Source: shared/can/can_{bus_name}.yaml | Regenerate: <code>python generate_can_tables.py</code></p>")

    for msg in sorted(messages, key=lambda m: m.id):
        cycle = f"{msg.cycle_ms}ms" if msg.cycle_ms else "Event"
        rx = ", ".join(msg.receivers) if msg.receivers else "All"
        lines.append(f"<h2>0x{msg.id:03X} — {msg.name}</h2>")
        lines.append(f"<p class=meta>DLC={msg.dlc} | {cycle} | {msg.sender} → {rx}</p>")
        if msg.comment:
            lines.append(f"<p class=comment>{clean(msg.comment)}</p>")

        if not msg.signals:
            lines.append('<p class=no-signals>(DLC=0 event frame — no signals)</p>')
            continue

        lines.append("<table><tr><th>Signal</th><th>Byte</th><th>Bit</th><th>Len</th><th>Type</th><th>Scale</th><th>Unit</th><th>Values</th><th>Description</th></tr>")
        for sig in msg.signals:
            vals = ""
            if sig.values:
                vals = ", ".join(f"{k}={v}" for k, v in sorted(sig.values.items()))
            desc = clean(sig.comment or "")
            lines.append(
                f"<tr><td><b>{sig.name}</b></td><td>{sig.byte}</td><td>{sig.bit_offset}</td>"
                f"<td>{sig.size}</td><td>{type_str(sig)}</td><td>{scale_str(sig)}</td>"
                f"<td>{sig.unit or '-'}</td><td>{vals}</td><td>{desc}</td></tr>"
            )
        lines.append("</table>")

    lines.append(f"<p class=meta style='margin-top:40px'>Generated from shared/can/can_{bus_name}.yaml</p>")
    lines.append("</body></html>")
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
    write_html("high", high, OUT_DIR / "can_table_high.html")
    write_html("low", low, OUT_DIR / "can_table_low.html")
    print(f"Done: high={len(high)} msgs, low={len(low)} msgs")

if __name__ == "__main__":
    main()
