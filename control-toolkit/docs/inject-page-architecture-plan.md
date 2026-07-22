# Inject page — UX architecture plan

**Date:** 2026-07-22  
**Status:** Implemented (2026-07-22) — see `Inject.tsx` dense layout  
**Component today:** `frontend/src/components/Inject.tsx`  
**Screenshot (current):** [`artifacts/inject-page-ss.png`](../../artifacts/inject-page-ss.png) · viewport [`artifacts/inject-page-viewport.png`](../../artifacts/inject-page-viewport.png)

---

## 1. Problem statement

The Inject workspace tries to be **session gate + mode switch + full signal editor + controller manager + templates + live value map + job table + ack log** on one scroll surface. It works, but it is hard to scan, wastes vertical space, and splits “what is running” across three places.

### What the screenshot shows (current)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Title + long marketing description                                       │
├──────────────────────────────────────────────────────────────────────────┤
│ Gate panel: Session · Bench TX · Wire Preview · [Arm Bench TX]           │
│ Mode: Named | Raw                                                         │
├───────────────────────────────────┬──────────────────────────────────────┤
│ Signal Generator                  │ Started Controllers (0) + prose      │
│ HIGH | LOW                        │ Presets & Templates (7 fat cards)    │
│ Filter + CAN Message dropdown     │ Live Value Map (redundant)           │
│ ESTOP confirm / fields / period   │                                      │
│ [Send Once]                       │                                      │
├───────────────────────────────────┴──────────────────────────────────────┤
│ Active Periodic Transmitters (full table, even when empty)               │
├──────────────────────────────────────────────────────────────────────────┤
│ Command Acks / Transmit Log (another tall panel)                         │
└──────────────────────────────────────────────────────────────────────────┘
```

### Pain points

| Issue | Why it hurts |
|-------|----------------|
| **Active jobs live at the bottom** | Operator must scroll past the editor to stop something |
| **Started Controllers cards are large** | Status, full summary, Start/Stop/Load per card — too much chrome for “data + stop” |
| **Duplicate state models** | `activeJobs` (backend jobs) **and** `startedControllers` (UI memory) + “Live Value Map” of the form |
| **Templates compete with actives** | Right column is half “presets” before you ever have something running |
| **Gate panel is a full card** | Session / TX / wire are useful but should be a dense toolbar, not a second header |
| **Wire preview in gate + meta in title row** | Same information twice |
| **Empty states still reserve height** | “No controllers…”, full empty jobs table, empty value map, empty log |
| **Raw mode** | Same sprawl pattern with a second side list of fault presets |

User ask (paraphrased): **active ones summarized on the right, stoppable, compact data only; current right side takes too much space; overall is a mess.**

---

## 2. Goals

1. **Primary job in one glance:** pick message → set signals → send / start periodic.  
2. **Always-visible active TX:** right rail = compact running (and stopped-but-owned) list with **Stop** / **Stop all**.  
3. **Dense data, not cards:** one line per job (bus · id · name · period · status · stop).  
4. **No triple bookkeeping** for the same transmitter.  
5. **Templates as secondary** (drawer or collapsed), not permanent right-column bulk.  
6. **Logs optional** (collapsed footer or “show log”), not a permanent third full panel.  
7. Keep safety: Bench TX arm, ESTOP confirm, raw confirm.

Non-goals (this plan): new backend inject APIs; changing YAML encode rules; Drive/Control teleop.

---

## 3. Target information architecture

### 3.1 Layout (named mode)

```
┌─ Inject toolbar (1 row) ─────────────────────────────────────────────────┐
│ TX ● Armed | Named ▌ Raw | Wire: 00 FF … · DLC 8     [Stop all n]        │
└──────────────────────────────────────────────────────────────────────────┘
┌─ Editor (main) ─────────────────────────────┬─ Active TX (sticky right) ─┐
│ Bus  [High] [Low]   Message [select ▾]  🔍  │ ACTIVE  2          Stop all│
│                                             │ · HIGH 0x300 HostDrive     │
│ Signals (compact form grid)                 │   10 ms  ok        [Stop]  │
│  speed  [====|===] 2000   gear [D ▾]        │ · LOW  0x7FE SysHb         │
│  yaw    [====|===] 0                        │   100 ms miss:0    [Stop]  │
│                                             │────────────────────────────│
│ [ ] Periodic  period [10] ms                │ RECENT (optional, 3 lines) │
│ [Send once]  [Start loop]                   │ 12:01 HIGH 0x300 oneshot ✓ │
└─────────────────────────────────────────────┴────────────────────────────┘
│ Templates ▸ (collapsed) · Raw presets when mode=raw                      │
└──────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Zones

| Zone | Content | Density rule |
|------|---------|--------------|
| **A. Toolbar** | Bench TX state + Arm if off; Named/Raw; live wire hex; Stop all if n>0 | Single 36–40 px row; no large card |
| **B. Editor (left ~65–70%)** | Bus, message pick, signal fields, period, primary actions | Only the selected message’s controls |
| **C. Active TX (right ~30%, sticky)** | Backend periodic jobs (+ optional oneshot last) as **rows**, Stop per row | **Data first**; no multi-button cards |
| **D. Templates** | Collapsed by default; expand as dropdown/popover or bottom strip | Not permanent half-column |
| **E. Log** | Last 5 lines mini, or collapsible “Transmit log” | Full table only when expanded |

### 3.3 Right rail — “Active TX” contract

**Source of truth:** backend periodic jobs (`activeJobs` / inject job list API), not a separate “Started Controllers” social-card model.

Each row (minimum fields):

| Field | Example |
|-------|---------|
| Bus | `H` / `L` chip |
| ID | `0x300` |
| Name / key short | `HOST_DRIVE_CMD` |
| Period | `10 ms` |
| Health | `ok` / `miss:n` / last result |
| Action | **Stop** only (primary) |

Optional affordances (icon or overflow, not a second row of buttons):

- Click row → **load into editor** (replaces “Load Settings”)  
- Double-click or “⋯” → start again only if stopped and we keep a stopped-history list  

**Stopped history:** either drop it (user re-picks message) or keep last N as muted rows under ACTIVE with single **Start** — still one-line, not fat cards.

**Empty right rail:**

```
No active TX
Start a periodic inject from the editor.
```

One short line — no essay.

### 3.4 Editor (left) — simplify

Keep:

- Bus High/Low  
- Message select (+ compact filter in the same control if possible)  
- Signal editors (slider + number OK; reduce preset chip walls — max 3–4 chips or “presets ▾”)  
- Periodic toggle + period  
- Send once / Start loop  
- ESTOP / safety confirm when needed  

Remove from permanent UI:

- Long page description (or one-line under title)  
- Duplicate wire preview in title and gate  
- “Live Value Map” on the right (editor already shows values)  
- Large “Started Controllers” cards  
- Full-width “Active Periodic Transmitters” section (moves to right rail)

### 3.5 Mode: Named vs Raw

Same shell:

- **Named:** editor = dictionary message + fields  
- **Raw:** editor = bus + id + hex + extended + confirm; right rail still shows **any** active periodic jobs (including raw if API supports); presets become collapsed “Fault presets”

### 3.6 State model cleanup

| Today | Target |
|-------|--------|
| `activeJobs` from backend | **Keep** — right rail |
| `startedControllers` local RUNNING/STOPPED cards | **Remove or shrink** to optional “recent recipes” under rail |
| Live Value Map | **Delete** (redundant with form) |
| Ack log full panel | **Ring buffer**, collapsed |

Unifying identity for a row: `job_id` when running; when loading editor from a job, use `key` + `bus` + `values` + `period_ms`.

---

## 4. Interaction design

### Happy path (periodic)

1. Arm Bench TX (toolbar).  
2. Select High + `HOST_DRIVE_CMD`, set speed/gear.  
3. Check Periodic, period 10 ms → **Start loop**.  
4. Right rail gains one row; **Stop** kills that job without scrolling.  
5. **Stop all** in toolbar or rail header clears every job.

### Happy path (oneshot)

1. Configure message → **Send once**.  
2. No permanent right-rail entry (or flash “last TX” for 3 s).  
3. Optional mini log line.

### Safety

- Bench TX off: primary actions disabled; Arm in toolbar (amber).  
- ESTOP named: confirm checkbox still required before send.  
- Raw: confirm remains.  
- Stop is always available when jobs exist, even if editor is mid-edit.

### Keyboard / density (stretch)

- Focus period field + Enter starts loop when periodic is on.  
- Dense form: 2-column signal grid on wide screens; single column when narrow.

---

## 5. Visual density rules (align with UI design guidelines)

- Prefer **table/list rows** over multi-line cards for active TX.  
- Right rail width ~ **240–280 px** (today up to 320 + verbose cards).  
- Row height ~ **32–36 px**; mono for IDs and periods.  
- One primary button in editor; Stop uses secondary/danger text, not full-width primary.  
- Collapse empty sections (no empty jobs table).  
- Templates: chevron section default **closed** if any job is active; open by default only when zero actives (optional).

---

## 6. Component structure (implementation sketch)

```
InjectPage
├── InjectToolbar          // TX, mode, wire hex, stop-all
├── InjectLayout           // CSS grid: main | rail
│   ├── InjectEditor       // named | raw
│   │   ├── BusMessagePicker
│   │   ├── SignalFieldList  // existing field widgets, tighter presets
│   │   └── InjectActions    // send / start
│   └── ActiveTxRail         // sticky
│       ├── ActiveTxHeader   // count + stop all
│       ├── ActiveTxRow[]    // data + stop
│       └── RecentTxMini?    // optional 3 lines
├── TemplatesDrawer          // collapsed
└── TransmitLogDrawer        // collapsed
```

File split (optional, after layout works in one file):

- `Inject.tsx` shell  
- `inject/ActiveTxRail.tsx`  
- `inject/InjectEditor.tsx`  
- `inject/templates.ts` (TEMPLATES / RAW_PRESETS)

Keep existing `data-testid`s where possible (`inject-submit`, `inject-stop-all`, bus tabs, etc.) and add:

- `inject-active-rail`  
- `inject-active-row-{jobId}`  
- `inject-active-stop-{jobId}`

---

## 7. Migration steps (PR-sized)

| Step | Work | Risk |
|------|------|------|
| **1** | Move Active jobs UI into right sticky rail; delete bottom full jobs panel | Low |
| **2** | Replace Started Controller cards with one-line rows driven by `activeJobs`; Stop wired | Medium (controller start/stop parity) |
| **3** | Collapse gate into toolbar; drop Live Value Map | Low |
| **4** | Templates → collapsible section; trim preset chip noise | Low |
| **5** | Ack log → collapsible mini log | Low |
| **6** | Remove or demote `startedControllers` if redundant; E2E update | Medium |

Ship order: **1 → 2 → 3** delivers the user’s “right side summary + stop, less space” request.

---

## 8. Acceptance criteria

- [ ] Without scrolling the main editor, user can see every **running** inject and **stop** any one.  
- [ ] Right rail empty state is ≤ 2 lines of text.  
- [ ] No separate full-width “Active Periodic Transmitters” panel when rail exists.  
- [ ] No “Live Value Map” panel.  
- [ ] Started-controller **cards** gone; rows are single-line data + Stop.  
- [ ] Templates do not dominate the right column by default.  
- [ ] Named + Raw still work; Bench TX / ESTOP / raw confirms preserved.  
- [ ] E2E: start periodic → row appears → stop → row gone; stop-all clears n jobs.

---

## 9. Out of scope / later

- Multi-select message inject  
- Graph of job rate vs bus load  
- Saving named “scenes” to disk  
- Merging Inject with Control keyboard (different ownership: inject = frames, control = intent)

---

## 10. Before / after (conceptual)

**Before:** Editor left; fat cards + templates + value map right; jobs table and log below → long scroll, stop actions buried.

**After:** Dense toolbar; editor left; **sticky Active TX data list right with Stop**; templates/log collapsed → scan and kill transmitters without leaving the form.

---

## 11. Reference

- Current implementation: `control-toolkit/frontend/src/components/Inject.tsx`  
- Styles: `.inject-layout`, `.started-controller-*`, `.inject-template-*` in `App.css`  
- Screenshots: `artifacts/inject-page-ss.png`, `artifacts/inject-page-viewport.png`  
- UI rules: `ui-design-control-toolkit.md` (density, tables, one primary action)
