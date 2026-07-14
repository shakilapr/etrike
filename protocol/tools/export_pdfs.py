#!/usr/bin/env python3
"""Generate printable A3 PDF CAN signal tables from YAML using Playwright."""

from pathlib import Path
from can_signals_schema import load_can_database_dir

CAN_DIR = Path(__file__).resolve().parent
OUT_DIR = CAN_DIR.parent.parent / "tem"

# ── Compact print-optimised HTML ────────────────────────────────────────

CSS = """
@page{size:A3 landscape;margin:12mm 10mm 14mm 10mm}
*{box-sizing:border-box;margin:0;padding:0}
body{font:7.5pt/1.25 system-ui,sans-serif;color:#1a1a1a}
h1{font-size:11pt;border-bottom:1.5pt solid #222;padding-bottom:3pt;margin-bottom:4pt}
.subtitle{font-size:6.5pt;color:#666;margin-bottom:8pt}
.columns{column-count:2;column-gap:16pt;column-rule:0.5pt solid #ddd}
.msg-block{break-inside:avoid;border:0.5pt solid #d0d0d0;border-radius:3pt;margin-bottom:7pt;padding:5pt 6pt 4pt;background:#fff}
.msg-hdr{display:flex;align-items:baseline;gap:6pt;margin-bottom:2pt}
.msg-id{font:600 8pt 'Cascadia Code','Consolas',monospace;color:#2563eb}
.msg-name{font-weight:600;font-size:8pt}
.msg-meta{font-size:6pt;color:#555;margin-bottom:3pt}
.msg-comment{font-size:6pt;font-style:italic;color:#777;margin-bottom:3pt}
.msg-layout{font:5.5pt/1.2 'Cascadia Code','Consolas',monospace;color:#555;margin-bottom:3pt;white-space:pre}
.msg-layout b{color:#2563eb;font-weight:600}
table{width:100%;border-collapse:collapse;font-size:6pt}
th{background:#f0f1f3;font-weight:600;text-align:left;padding:2pt 4pt;border-bottom:1pt solid #c0c4cc;text-transform:uppercase;letter-spacing:.02em}
td{padding:1.5pt 4pt;border-bottom:0.4pt solid #e8e8e8;vertical-align:top}
tr:nth-child(even) td{background:#fafbfc}
.sig-dot{display:inline-block;width:5pt;height:5pt;border-radius:1pt;margin-right:3pt;vertical-align:middle}
.empty{color:#bbb;font-style:italic}
.footer{font-size:6pt;color:#999;margin-top:10pt;border-top:0.5pt solid #ddd;padding-top:3pt}
"""

SIG_COLORS = [
    '#2563eb','#dc2626','#16a34a','#ca8a04','#9333ea','#0891b2','#e11d48','#65a30d',
    '#7c3aed','#0d9488','#ea580c','#4f46e5','#059669','#d97706','#6366f1','#0284c7',
]

def esc(s):
    if not s: return ''
    return s.replace('&','&amp;').replace('<','&lt;').replace('>','&gt;')

def dash(s):
    return esc(s) if s else '<span class=empty>&mdash;</span>'

def values_str(sig):
    if not sig.get('values'): return ''
    return '; '.join(f'{k}={v}' for k,v in sorted(sig['values'].items()))

def scale_str(sig):
    f, o = sig['factor'], sig['offset']
    if f == 1 and o == 0: return '1'
    if o == 0: return f'&times;{f:g}'
    return f'&times;{f:g} + {o:g}'

def type_str(sig):
    return 'signed' if sig['type'] == 'signed' else 'unsigned'

def layout_text(msg):
    """Compact text layout: one line per signal showing byte range and bits."""
    if msg['dlc'] == 0:
        return '  DLC=0 — event frame, no payload\n'
    lines = []
    for i, sig in enumerate(msg['signals']):
        c = SIG_COLORS[i % len(SIG_COLORS)]
        start_bit = sig['byte'] * 8 + sig['bit_offset']
        end_bit = start_bit + sig['size'] - 1
        start_byte = start_bit // 8
        end_byte = end_bit // 8
        if start_byte == end_byte:
            span = f"B{start_byte}[{sig['bit_offset']}:{sig['bit_offset']+sig['size']-1}]"
        else:
            span = f"B{start_byte}-{end_byte} ({sig['size']}b)"
        lines.append(f'  <b style="color:{c}">{esc(sig["name"])}</b>  {span}  {type_str(sig)} &times;{sig["factor"]:g}{" + "+str(sig["offset"]) if sig["offset"] else ""}  {esc(sig.get("unit") or "")}')
    return '\n'.join(lines)

def build_msg_block(msg):
    cycle = f'{msg["cycle_ms"]}ms' if msg['cycle_ms'] else 'Event'
    rx = ' / '.join(msg['receivers']) if msg['receivers'] else 'All'

    html = f'<div class=msg-block>'
    html += f'<div class=msg-hdr><span class=msg-id>0x{msg["id"]:03X}</span><span class=msg-name>{esc(msg["name"])}</span></div>'
    html += f'<div class=msg-meta>DLC={msg["dlc"]} &middot; {cycle} &middot; <b>{esc(msg["sender"])}</b> &rarr; {esc(rx)}</div>'
    if msg.get('comment'):
        html += f'<div class=msg-comment>{esc(msg["comment"])}</div>'
    html += f'<div class=msg-layout>{layout_text(msg)}</div>'

    if msg['signals']:
        html += '<table><tr><th>Signal</th><th>B</th><th>Bit</th><th>Len</th><th>Type</th><th>Scale</th><th>Unit</th><th>Values</th></tr>'
        for i, sig in enumerate(msg['signals']):
            c = SIG_COLORS[i % len(SIG_COLORS)]
            html += (f'<tr>'
                f'<td><span class=sig-dot style="background:{c}"></span><b>{esc(sig["name"])}</b></td>'
                f'<td>{sig["byte"]}</td><td>{sig["bit_offset"]}</td><td>{sig["size"]}</td>'
                f'<td>{type_str(sig)}</td><td>{scale_str(sig)}</td>'
                f'<td>{dash(sig.get("unit"))}</td><td>{dash(values_str(sig))}</td></tr>')
        html += '</table>'
    else:
        html += '<div class=empty>DLC=0 event frame — no payload</div>'

    html += '</div>'
    return html

from protocol import load_model, validate_model

def _field_limits(field: dict) -> tuple[float, float]:
    bits = field["bits"]
    if field.get("signed"):
        default_min, default_max = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        default_min, default_max = 0, (1 << bits) - 1
    factor = field.get("factor", 1.0)
    offset = field.get("offset", 0.0)
    return field.get("min", default_min * factor + offset), field.get("max", default_max * factor + offset)

def build_html(bus_name, messages):
    parts = ['<div class=columns>']
    for msg in messages:
        parts.append(build_msg_block(msg))
    parts.append('</div>')
    parts.append(f'<div class=footer>E-Trike CAN Signal Reference &mdash; {bus_name.title()} Bus &mdash; A3 Landscape</div>')
    return '\n'.join(parts)

def generate_pdf(bus_name, messages, out_path):
    """Generate A3 PDF via Playwright (Chromium headless)."""
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print(f"Skipping PDF generation for {bus_name} (playwright not installed). Install with: pip install playwright && playwright install chromium")
        return

    body = build_html(bus_name, messages)
    html = f"""<!DOCTYPE html><html><head><meta charset=utf-8>
<title>E-Trike CAN — {bus_name.title()} Bus</title><style>{CSS}</style></head><body>
<h1>E-Trike CAN Signal Reference &mdash; {bus_name.title()} Bus</h1>
<p class=subtitle>Generated from protocol contracts &mdash; A3 landscape &mdash; {len(messages)} messages</p>
{body}
</body></html>"""

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.set_content(html, wait_until='networkidle')
        page.wait_for_timeout(500)
        page.pdf(
            path=str(out_path),
            format='A3',
            landscape=True,
            print_background=True,
            margin={'top': '0', 'right': '0', 'bottom': '0', 'left': '0'},
        )
        browser.close()
    print(f'  Wrote {out_path}')

def main():
    model = load_model()
    validated = validate_model(model, check_baseline=False)
    
    network = validated["network"]
    instances = validated["instances"]
    
    buses = [bus.get("key") for bus in network.get("buses", [])]
    
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    
    for bus in buses:
        bus_list = []
        for identity, (key, message, instance) in instances.items():
            if instance["bus"] != bus:
                continue
                
            frame_id = instance["id"]
            if isinstance(frame_id, str):
                frame_id = int(frame_id, 16 if frame_id.lower().startswith("0x") else 10)
                
            msg_dict = {
                "id": frame_id,
                "name": message["name"],
                "dlc": message["dlc"],
                "sender": instance["sender"],
                "receivers": instance.get("receivers", []),
                "cycle_ms": instance.get("cycle_ms", 0),
                "comment": message.get("comment", ""),
                "signals": []
            }
            
            layout = message.get("layout", {})
            if layout.get("kind") == "signals":
                for field in layout.get("fields", []):
                    minimum, maximum = _field_limits(field)
                    msg_dict["signals"].append({
                        "name": field.get("name", field.get("key")),
                        "byte": field["byte"],
                        "bit_offset": field["bit"],
                        "size": field["bits"],
                        "type": "signed" if field.get("signed") else "unsigned",
                        "factor": field.get("factor", 1.0),
                        "offset": field.get("offset", 0.0),
                        "min": minimum,
                        "max": maximum,
                        "unit": field.get("unit", ""),
                        "values": {str(k): str(v) for k, v in field["enum"].items()} if "enum" in field else None,
                        "comment": field.get("comment", "")
                    })
            bus_list.append(msg_dict)
            
        bus_list.sort(key=lambda m: m["id"])
        
        if bus_list:
            generate_pdf(bus, bus_list, OUT_DIR / f'can_table_{bus}.pdf')
            print(f'Done {bus}: {len(bus_list)} msgs')

if __name__ == '__main__':
    main()
