#!/usr/bin/env python3
"""Generate interactive CAN signal viewer as a self-contained HTML page from shared/can/*.yaml."""

from pathlib import Path
from can_signals_schema import load_can_database_dir
import json

CAN_DIR = Path(__file__).resolve().parent
OUT_DIR = CAN_DIR.parent.parent / "tem"

HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>E-Trike CAN Signal Viewer</title>
<style>
:root{
  --bg:#fff;--fg:#1a1a1a;--meta:#5a5a5a;--border:#e2e4e8;--accent:#2563eb;
  --row-hover:#f0f4ff;--stripe:#fafbfc;--card-bg:#fafafa;--bit-0:#fff;--bit-1:#2563eb;
  --bit-hover:#1d4ed8;--sig-hover:#dbeafe;--th-bg:#f4f5f7;--th-border:#c0c4cc;
}
*,*::before,*::after{box-sizing:border-box}
body{font:14px/1.5 system-ui,sans-serif;max-width:none;margin:0 auto;padding:20px 28px;color:var(--fg);background:var(--bg)}
h1{font-size:20px;font-weight:600;border-bottom:1px solid var(--border);padding-bottom:10px;margin:0 0 4px}
.subtitle{color:var(--meta);font-size:13px;margin:0 0 20px}
.subtitle a{color:var(--accent)}

/* search + filters */
.toolbar{display:flex;gap:12px;margin-bottom:16px;flex-wrap:wrap;align-items:center}
#search{padding:8px 12px;border:1px solid var(--border);border-radius:6px;font-size:14px;width:280px;outline:none}
#search:focus{border-color:var(--accent);box-shadow:0 0 0 2px rgba(37,99,235,.15)}
.bus-tabs{display:flex;gap:0}
.bus-tab{padding:7px 18px;border:1px solid var(--border);background:var(--bg);cursor:pointer;font-size:13px;font-weight:500;color:var(--meta)}
.bus-tab:first-child{border-radius:6px 0 0 6px}
.bus-tab:last-child{border-radius:0 6px 6px 0}
.bus-tab.active{background:var(--accent);color:#fff;border-color:var(--accent)}
.bus-tab:hover:not(.active){background:var(--stripe)}
.count{color:var(--meta);font-size:13px;margin-left:auto}

/* message cards */
.msg-card{border:1px solid var(--border);border-radius:8px;margin-bottom:12px;overflow:visible;background:var(--bg)}
.msg-card.hidden{display:none}
.msg-header{cursor:pointer;user-select:none;background:var(--card-bg);border-radius:8px;overflow:hidden}
.msg-card.open .msg-header{border-radius:8px 8px 0 0}
.msg-header:hover{background:var(--row-hover)}
.msg-header-row{display:flex;align-items:center;gap:10px;padding:10px 14px 4px}
.msg-header .arrow{font-size:11px;color:var(--meta);transition:transform .2s;flex-shrink:0}
.msg-header .id{font:600 15px 'Cascadia Code','Fira Code','Consolas',monospace;color:var(--accent);flex-shrink:0}
.msg-header .name{font-weight:600;font-size:14px;flex-shrink:0}
.msg-meta{display:flex;align-items:center;gap:8px;padding:0 14px 8px;font-size:12px;color:var(--meta);flex-wrap:wrap}
.msg-meta .badge{display:inline-block;padding:2px 8px;border-radius:3px;font-size:11px;font-weight:500}
.badge-sender{background:#dbeafe;color:#1e40af}
.badge-receiver{background:#f0fdf4;color:#166534}
.badge-dlc{background:var(--stripe);color:var(--meta)}
.msg-header.open .arrow{transform:rotate(90deg)}

.msg-body{display:none;padding:14px;overflow:visible}
.msg-card.open .msg-body{display:block}
.msg-card.open .msg-header{border-bottom:1px solid var(--border)}
.msg-comment{color:var(--meta);font-style:italic;font-size:13px;margin:0 0 14px}

/* byte grid */
.byte-grid-scroll{overflow:visible;padding:10px 2px 2px}
.byte-grid{display:flex;flex-wrap:wrap;gap:2px 8px;margin-bottom:16px;overflow:visible;min-width:0}
.table-scroll{overflow-x:auto;margin-top:12px}
.byte-col{display:flex;flex-direction:column;gap:2px;align-items:center}
.byte-label{font:10px 'Cascadia Code','Fira Code','Consolas',monospace;color:var(--meta)}
.bit-grid{display:flex;flex-direction:column;gap:2px;overflow:visible}
.bit-row{display:flex;gap:2px}
.bit-cell{width:18px;height:18px;border-radius:2px;border:1px solid var(--border);cursor:pointer;transition:box-shadow .12s;position:relative}
.bit-cell.empty{background:var(--bit-0)}
.bit-cell.filled{background:var(--bit-1);border-color:var(--bit-1)}
.bit-cell.wide{width:15px;height:15px}
.bit-cell:hover{z-index:5;outline:0;box-shadow:inset 0 0 0 2px #000,0 0 0 1px rgba(0,0,0,.35)}
.bit-cell.highlight{z-index:3;box-shadow:inset 0 0 0 2px #000}

/* signal info tooltip on bit hover */
.bit-tooltip{display:none}
.bit-tooltip span{display:block}
.bit-tooltip b{color:#93c5fd}
.bit-hover-panel{display:none;position:fixed;z-index:99999;pointer-events:none;background:#1a1a1a;color:#fff;font-size:11px;
  padding:7px 10px;border-radius:4px;white-space:normal;min-width:190px;max-width:min(340px,80vw);text-align:left;line-height:1.35;
  box-shadow:0 8px 24px rgba(0,0,0,.35)}
.bit-hover-panel.visible{display:block}
.bit-hover-panel span{display:block}
.bit-hover-panel b{color:#93c5fd}

/* signal table */
.sig-table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px}
.sig-table th{background:var(--th-bg);color:var(--fg);padding:7px 10px;text-align:left;font-weight:600;font-size:11px;text-transform:uppercase;letter-spacing:.03em;border-bottom:2px solid var(--th-border);position:sticky;top:0}
.sig-table td{padding:6px 10px;border-bottom:1px solid var(--border);vertical-align:top}
.sig-table tr.sig-highlight td{background:var(--sig-hover)}
.sig-color{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:middle}
.empty{color:#bbb;font-style:italic}
.no-results{text-align:center;color:var(--meta);padding:40px;font-size:15px}

/* reference link */
.ref-link{font-size:13px;color:var(--meta);margin-top:40px;padding-top:12px;border-top:1px solid var(--border)}
.ref-link a{color:var(--accent)}
</style>
</head>
<body>
<h1>E-Trike CAN Signal Viewer</h1>
<p class=subtitle>Interactive reference — see <a href="../docs/how-to-read-can-tables.md">How to Read CAN Tables</a> for legend</p>

<div class=toolbar>
  <input id=search type=text placeholder="Search by CAN ID or name…" autofocus>
  <div class=bus-tabs>
    <button class="bus-tab active" data-bus="all">All</button>
    <button class=bus-tab" data-bus="high">High Bus</button>
    <button class=bus-tab" data-bus="low">Low Bus</button>
  </div>
  <span class=count id=count></span>
</div>

<div id=messages></div>

<p class=ref-link>Generated from <code>shared/can/can_*.yaml</code> | <a href="../docs/how-to-read-can-tables.md">How to read CAN tables</a></p>

<script>
const DATA = __DATA_PLACEHOLDER__;
// { bus, messages: [{id, name, dlc, sender, receivers, cycle_ms, comment, signals:
//   [{name, byte, bit_offset, size, type, factor, offset, unit, values, comment}] }] }

const SIGNAL_COLORS = [
  '#2563eb','#dc2626','#16a34a','#ca8a04','#9333ea','#0891b2','#e11d48','#65a30d',
  '#7c3aed','#0d9488','#ea580c','#4f46e5','#059669','#d97706','#6366f1','#0284c7',
  '#b91c1c','#15803d','#a16207','#6d28d9'
];

let activeBus = 'all';

function colorForIndex(i) { return SIGNAL_COLORS[i % SIGNAL_COLORS.length]; }

function escapeHtml(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function orDash(s) {
  return (s === undefined || s === null || s === '') ? '<span class=empty>&mdash;</span>' : escapeHtml(s);
}

function scaleStr(sig) {
  const f = sig.factor, o = sig.offset;
  if (f === 1 && o === 0) return '1';
  if (o === 0) return `&times;${f}`;
  if (f === 1) return o >= 0 ? `+${o}` : `${o}`;
  return `&times;${f} + ${o}`;
}

function typeStr(sig) {
  return sig.type === 'signed' ? 'signed' : 'unsigned';
}

function valuesStr(sig) {
  if (!sig.values || Object.keys(sig.values).length === 0) return '';
  return Object.entries(sig.values).map(([k,v]) => `${k}=${v}`).join(', ');
}

function bitMeaningStr(sig, byte, bit) {
  const relativeBit = (byte * 8 + bit) - (sig.byte * 8 + sig.bit_offset);
  const commentMatch = sig.comment ? sig.comment.match(new RegExp(`bit${relativeBit}\\s*=\\s*([^,;]+)`, 'i')) : null;
  if (commentMatch && commentMatch[1]) return commentMatch[1].trim();

  const mask = String(2 ** relativeBit);
  if (sig.values && sig.values[mask]) return sig.values[mask];

  if (sig.size === 1) {
    const activeName = sig.name
      .replace(/^[A-Z0-9]+_/, '')
      .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
      .replace(/_/g, ' ')
      .toLowerCase();
    return `1 = ${activeName || 'active'}, 0 = inactive`;
  }

  return '';
}

function bitTooltip(sig, byte, bit) {
  const bitMeaning = bitMeaningStr(sig, byte, bit);
  const lines = [
    `<b>${escapeHtml(sig.name)}</b>`,
    `B${byte}.${bit} &middot; ${sig.size}-bit ${typeStr(sig)}`,
    bitMeaning ? `Bit meaning: ${escapeHtml(bitMeaning)}` : '',
    `Scale: ${escapeHtml(scaleStr(sig))}${sig.unit ? ' ' + escapeHtml(sig.unit) : ''}`,
    valuesStr(sig) ? `Values: ${escapeHtml(valuesStr(sig))}` : '',
    sig.comment ? escapeHtml(sig.comment) : ''
  ].filter(Boolean);
  return `<span class=bit-tooltip>${lines.map(line => `<span>${line}</span>`).join('')}</span>`;
}

function buildBitGrid(msg) {
  if (msg.dlc === 0) return '<div class=empty style="margin-bottom:14px">DLC=0 — no payload (event frame)</div>';

  // Map each bit to its signal index
  const bitMap = new Array(msg.dlc * 8).fill(-1);
  msg.signals.forEach((sig, si) => {
    const start = sig.byte * 8 + sig.bit_offset;
    for (let i = 0; i < sig.size; i++) {
      if (start + i < bitMap.length) bitMap[start + i] = si;
    }
  });

  // Build HTML: B7 (MSB) at top, B0 (LSB) at bottom
  // Within each byte row: bit 7 (left) to bit 0 (right)
  const wide = msg.dlc >= 7 ? ' wide' : '';
  let cols = '';
  for (let b = 0; b < msg.dlc; b++) {
    let cells = '';
    for (let bit = 7; bit >= 0; bit--) {
      const absBit = b * 8 + bit;
      const si = bitMap[absBit];
      const filled = si >= 0;
      const color = filled ? colorForIndex(si) : '';
      const tooltip = filled
        ? bitTooltip(msg.signals[si], b, bit)
        : '';
      cells += `<div class="bit-cell${wide}${filled ? ' filled' : ' empty'}"`
        + (filled ? ` style="background:${color};border-color:${color}"` : '')
        + ` data-signal="${si}">${tooltip}</div>`;
    }
    cols += `<div class=byte-col>`
      + `<span class=byte-label>B${b}</span>`
      + `<div class=bit-grid><div class=bit-row>${cells}</div></div>`
      + `</div>`;
  }
  return `<div class=byte-grid-scroll><div class=byte-grid>${cols}</div></div>`;
}

function buildSignalTable(msg) {
  let rows = '';
  msg.signals.forEach((sig, i) => {
    const color = colorForIndex(i);
    rows += `<tr data-signal="${i}">`
      + `<td><span class=sig-color style="background:${color}"></span><b>${escapeHtml(sig.name)}</b></td>`
      + `<td>${sig.byte}</td><td>${sig.bit_offset}</td><td>${sig.size}</td>`
      + `<td>${typeStr(sig)}</td><td>${scaleStr(sig)}</td>`
      + `<td>${orDash(sig.min)}</td><td>${orDash(sig.max)}</td>`
      + `<td>${orDash(sig.unit)}</td><td>${orDash(valuesStr(sig))}</td>`
      + `<td>${orDash(sig.comment)}</td></tr>`;
  });
  return `<table class=sig-table><thead><tr>`
    + '<th>Signal</th><th>Byte</th><th>Bit</th><th>Len</th>'
    + '<th>Type</th><th>Scale</th><th>Min</th><th>Max</th><th>Unit</th><th>Values</th><th>Description</th>'
    + '</tr></thead><tbody>' + rows + '</tbody></table>';
}

function buildMessageCard(msg) {
  const cycle = msg.cycle_ms ? `${msg.cycle_ms}ms` : 'Event';
  const rxList = msg.receivers && msg.receivers.length ? msg.receivers : ['All'];
  const rxBadges = rxList.map(r => `<span class=\"badge badge-receiver\">${escapeHtml(r)}</span>`).join(' ');
  const senderBadge = `<span class=\"badge badge-sender\">${escapeHtml(msg.sender)}</span>`;
  const dlcBadge = `<span class=\"badge badge-dlc\">DLC=${msg.dlc}</span>`;
  return `<div class="msg-card open" data-id="${msg.id}" data-name="${escapeHtml(msg.name).toLowerCase()}" data-bus="${msg._bus}">`
    + `<div class="msg-header open" onclick="this.parentElement.classList.toggle('open');this.classList.toggle('open')">`
    + `<div class=msg-header-row>`
    + `<span class=arrow>&#9654;</span>`
    + `<span class=id>0x${msg.id.toString(16).toUpperCase().padStart(3,'0')}</span>`
    + `<span class=name>${escapeHtml(msg.name)}</span>`
    + `</div>`
    + `<div class=msg-meta>${dlcBadge} &middot; ${cycle} &middot; ${senderBadge} &rarr; ${rxBadges}</div>`
    + `</div>`
    + `<div class=msg-body>`
    + (msg.comment ? `<p class=msg-comment>${escapeHtml(msg.comment)}</p>` : '')
    + buildBitGrid(msg)
    + (msg.signals.length ? '<div class=table-scroll>' + buildSignalTable(msg) + '</div>' : '<div class=empty>DLC=0 event frame &mdash; no signals</div>')
    + `</div></div>`;
}

function renderAll(allMessages) {
  const container = document.getElementById('messages');
  let html = '';
  allMessages.forEach(m => html += buildMessageCard(m));
  container.innerHTML = html;
  ensureBitHoverPanel();
  attachBitHover(container);
}

function ensureBitHoverPanel() {
  if (document.getElementById('bit-hover-panel')) return;
  const panel = document.createElement('div');
  panel.id = 'bit-hover-panel';
  panel.className = 'bit-hover-panel';
  document.body.appendChild(panel);
}

function moveBitHoverPanel(event) {
  const panel = document.getElementById('bit-hover-panel');
  if (!panel) return;
  const width = 340, height = 180;
  const left = Math.max(12, Math.min(event.clientX + 16, window.innerWidth - width - 12));
  const top = Math.max(12, Math.min(event.clientY + 18, window.innerHeight - height - 12));
  panel.style.left = `${left}px`;
  panel.style.top = `${top}px`;
}

// Hover bit cell → highlight signal row
function attachBitHover(container) {
  container.querySelectorAll('.bit-cell[data-signal]').forEach(cell => {
    cell.addEventListener('mouseenter', (event) => {
      const si = cell.dataset.signal;
      const card = cell.closest('.msg-card');
      card.querySelectorAll(`tr[data-signal="${si}"]`).forEach(r => r.classList.add('sig-highlight'));
      card.querySelectorAll(`.bit-cell[data-signal="${si}"]`).forEach(c => c.classList.add('highlight'));
      const tooltip = cell.querySelector('.bit-tooltip');
      const panel = document.getElementById('bit-hover-panel');
      if (tooltip && panel) {
        panel.innerHTML = tooltip.innerHTML;
        panel.classList.add('visible');
        moveBitHoverPanel(event);
      }
    });
    cell.addEventListener('mousemove', moveBitHoverPanel);
    cell.addEventListener('mouseleave', () => {
      const si = cell.dataset.signal;
      const card = cell.closest('.msg-card');
      card.querySelectorAll(`tr[data-signal="${si}"]`).forEach(r => r.classList.remove('sig-highlight'));
      card.querySelectorAll(`.bit-cell[data-signal="${si}"]`).forEach(c => c.classList.remove('highlight'));
      document.getElementById('bit-hover-panel')?.classList.remove('visible');
    });
  });
}

function filter() {
  const query = document.getElementById('search').value.toLowerCase().trim();
  const cards = document.querySelectorAll('.msg-card');
  let visible = 0;
  cards.forEach(card => {
    const matchBus = activeBus === 'all' || card.dataset.bus === activeBus;
    const matchQuery = !query
      || card.dataset.id.toString(16).toUpperCase().includes(query.toUpperCase())
      || card.dataset.name.includes(query);
    const show = matchBus && matchQuery;
    card.classList.toggle('hidden', !show);
    if (show) visible++;
  });
  document.getElementById('count').textContent = `${visible} message${visible !== 1 ? 's' : ''}`;
}

// Init
document.addEventListener('DOMContentLoaded', () => {
  // Attach messages with bus info
  const all = [];
  DATA.high.forEach(m => { m._bus = 'high'; all.push(m); });
  DATA.low.forEach(m => { m._bus = 'low'; all.push(m); });
  renderAll(all);
  document.getElementById('count').textContent = `${all.length} messages`;

  // Search
  document.getElementById('search').addEventListener('input', filter);

  // Bus tabs
  document.querySelectorAll('.bus-tab').forEach(tab => {
    tab.addEventListener('click', () => {
      document.querySelectorAll('.bus-tab').forEach(t => t.classList.remove('active'));
      tab.classList.add('active');
      activeBus = tab.dataset.bus;
      filter();
    });
  });
});
</script>
</body>
</html>"""

def msg_to_dict(msg):
    """Convert a MessageDef to a JSON-serializable dict."""
    return {
        "id": msg.id,
        "name": msg.name,
        "dlc": msg.dlc,
        "sender": msg.sender,
        "receivers": msg.receivers,
        "cycle_ms": msg.cycle_ms,
        "comment": msg.comment,
        "signals": [
            {
                "name": s.name,
                "byte": s.byte,
                "bit_offset": s.bit_offset,
                "size": s.size,
                "type": s.type.value if s.type else "unsigned",
                "factor": s.factor,
                "offset": s.offset,
                "min": s.min,
                "max": s.max,
                "unit": s.unit,
                "values": {str(k): v for k, v in s.values.items()} if s.values else None,
                "comment": s.comment,
            }
            for s in msg.signals
        ],
    }

def main():
    db = load_can_database_dir(CAN_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    high, low = [], []
    high_seen, low_seen = set(), set()
    for _pname, proto in db.protocols.items():
        bus = proto.bus if proto.bus in ("high", "low") else "low"
        for msg in proto.messages:
            d = msg_to_dict(msg)
            if bus == "high" and msg.id not in high_seen:
                high.append(d); high_seen.add(msg.id)
            elif bus == "low" and msg.id not in low_seen:
                low.append(d); low_seen.add(msg.id)

    # Sort by ID
    high.sort(key=lambda m: m["id"])
    low.sort(key=lambda m: m["id"])

    data_json = json.dumps({"high": high, "low": low}, indent=2, ensure_ascii=False)
    output = HTML.replace("__DATA_PLACEHOLDER__", data_json)

    out_path = OUT_DIR / "can_viewer.html"
    out_path.write_text(output, encoding="utf-8")
    print(f"  Wrote {out_path}")
    print(f"Done: high={len(high)} msgs, low={len(low)} msgs, total={len(high)+len(low)}")

if __name__ == "__main__":
    main()
