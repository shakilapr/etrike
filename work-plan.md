# Control Toolkit / RT Bench — Work Plan (rev 2)

**Date:** 2026-09-14 (rev 2 after double-check + parallel-session discovery)
**Scope:** Fix "RT shows not connected while RT heartbeat is visible" + related UI / CAN-signal defects.
**Rev 2 changes:**
- Answers the protocol question: is widening `estop_reason` actually needed? (§3)
- Records that a parallel session already implemented WP-01/04/05/06/07 in the working tree (§5) — they are **not deployed yet** (backend not restarted, firmware not reflashed).
- Adds WP-09 (0x621 diag events invisible — the designed "why" channel is unused), WP-10 (topbar vs backend ESTOP report disagreement), WP-11 (estop_reason latch inconsistency: reasons 8/9 never stored).

---

## 1. Problem statement

On the live bench (profile `bench_test`, CANalyst-II, destination `physical`):

- Topbar ECU rail: **RT-H red (0x7FD High offline/missing)** while RT-L green; RT clearly alive
  (0x121 ~92 Hz High, 0x204 ~100 Hz Low, 0x501 both buses).
- Operators read RT-H red as "RT not connected".

Root causes are layered: firmware silently drops a periodic frame whose encode fails, the
bench firmware/build is stale, the backend mis-computes observed rates, and the UI trusts
stale frames and lacks presence fallbacks.

---

## 2. Evidence snapshot (2026-09-14, live bench)

| Observation | Value |
|---|---|
| Backend `/status` | ready · adapter `canalystii` active · link connected · both channels active |
| 0x7FD RT_HEARTBEAT | Low: live 2.0 Hz stable · **High: intermittent — 2 Hz bursts (~22/10.8 s) then silent for minutes** |
| 0x210 RT_STATE_RPT | **missing on both buses 22.5+ min** — while RT_DIAG_RPT 0x620 says MCP2515 healthy (TEC/REC=0, no bus-off, no SPI faults) |
| **0x621 RT_DIAG_EVENT_RPT** | **streaming `diag_id=0x0208 RtHostDriveCmdStale, state=ACTIVE, occ=1`** — RT is publishing the precise cause; toolkit ignores it |
| `observed_rate_hz` anomalies | 0x210: 48309 Hz · 0x621: 8818 Hz · 0x121: 256 Hz (single-gap artifacts) |
| Topbar ESTOP chip | "RT · CAN frame" (reason 5) from a **22-min-stale** frame; backend report said `estop_reason=0, frame_fresh=false, primary_cause="SYS brake fault"` |
| History 10.8 s | high: 0x121×995, 0x311×107, 0x501×97, 0x620×11, 0x621×195, 0x7FD×21, **0x210×0** |
| wire_hash | live backend `508fae8b…` ≠ regenerated contracts `ad2e5225…` → **backend process predates the fixes** |

---

## 3. The protocol question — do we need the protocol update?

**Short answer: the widened range is correct and should be kept, but it is not sufficient and
not the whole story. Your instinct about "the other message" is also right — both channels
matter.**

### What the code actually does (verified)

- `protocol/contracts/rt.yaml` `rt_state_rpt.estop_reason`: 4-bit field, was `max:7`.
- `rt-esp32/src/config.h:15-25` defines reasons **0–10** (`8=egas_mismatch, 9=stale_cmd, 10=watchdog`).
- `safety_monitor.h:191` **stores** `kEstopReasonWatchdog=10` (MTR feedback timeout in AUTO).
  **10 is the only out-of-range value ever stored** — nothing ever stores 8 or 9 (dead constants).
  The stale-command path (`main.cpp:1300-1313`) raises diag event `RtHostDriveCmdStale` but
  never writes `g_estop_reason` → 0x210 keeps reporting reason 0 while a stale-cmd fault is active.
- `main.cpp:1190-1195`: `g_estop_reason` **latches** — cleared only when `!zero_setpoints &&
  !disable_steering && mode != ESTOP`. On this bench (`no_sys_authority=1`, SYS estop latched)
  zero_setpoints is true every tick → a latched 10 **can never clear**.
- `main.cpp:519-538`: `RtStateRpt::pack` returns `ValueOutOfRange` for reason 10 →
  `encode_frame` fails → **the TX is silently skipped, no counter, no log** → 0x210 gone
  from both buses → UI loses RT mode/reason → RT-H lamp red → "RT not connected".

### The two design channels (per `protocol/diagnostics/diagnostics.yaml` + safety_monitor.h)

| Channel | Content | Granularity | Status |
|---|---|---|---|
| 0x210 `RT_STATE_RPT.estop_reason` | coarse reason indicator, every 100 ms | 4-bit code | was silently dead for reason 10 |
| 0x621 `RT_DIAG_EVENT_RPT` (diag_id + state + occurrence + snapshot) | precise cause with detail | event-driven, precise | **transmitted by RT, decoded by backend, consumed by nobody** |

### Decision (rev 2)

1. **Keep the widened `max:15`** (already regenerated in the working tree, tests updated).
   Rationale: firmware reality defines 0–10; the field is wire-layout-identical (still 4 bits);
   receivers decode any value; it restores the at-a-glance reason without new correlation logic.
   Reverting to 0-7 would force clamping in firmware + diag-event correlation in UI for the
   same information — strictly more work.
2. **But the range widening alone does NOT implement the plan** — because reasons must also
   flow through 0x621 (it carries snapshot data the 4-bit code never will, and covers any
   future code >15). Add the 0x621 consumer path (WP-09).
3. **Firmware must stop the silent drop regardless of range** (WP-01-f): a periodic frame
   must never be skipped silently on encode failure — clamp/saturate + count + log.
   Any future out-of-contract value must degrade to "visible with a code", not vanish.
4. **Fix the reason latch inconsistency** (WP-11): stale-cmd / egas-mismatch paths must store
   their reason into `g_estop_reason` (or the field must be derived from latched diag state),
   so 0x210 and 0x621 tell the same story.

### What does NOT need protocol change

- Frame layout/DLC of 0x210 — unchanged.
- 0x500/0x501 cycle-time contract fixes (200 ms / 100 ms) — contract-follows-code, no ECU change.
- 0x122 wheel-speed decision (§4 A3) is firmware-side, no contract change if reserved.

---

## 4. Issue register (rev 2)

| ID | Severity | Layer | Title | Status |
|---|---|---|---|---|
| WP-01 | Critical | protocol+fw | `estop_reason=10` → 0x210 encode fails → silent TX skip | **contract widened 0-15 + regen + codec tests done in tree**; firmware drops instrumented on both buses |
| WP-01-f | High | firmware | 0x210 encode failure silently skipped — instrumented with counter/log on High & Low | **done in tree** (`rt-esp32/src/main.cpp:526-545, 1030-1040`) + 10 Hz Low bus TX added |
| WP-02 | High | firmware | 0x7FD High intermittent (bursts then silence) — suspect stale bench build | **open** — reflash + serial verify |
| WP-03 | Medium | firmware | 0x122 RT_WHEEL_SPEED_STS never sent despite contract | **open** — implement or mark reserved |
| WP-04 | Medium | contract | 0x500 cycle 20→200 ms, 0x501-high 20→100 ms | **done in tree** (contract-follows-code) |
| WP-05 | Medium | backend | observed_rate poisoned by single gap | **done in tree** (median of 8 gaps, burst filter, 5× clamp) + `test_latest_rate.py` verified; **needs backend restart** |
| WP-06 | High | frontend | stale RT_STATE_RPT feeds ESTOP chip | **done in tree** (`rtStale` + lastKnown* in signals.ts, Diagnostics renders stale state); drive chip shows "(stale)" |
| WP-07 | High | frontend | RT-H lamp no presence fallback | **done in tree** (`presenceFallbacks` 0x121/0x501/0x620 + tooltip note + harness status hints) |
| WP-08 | Low | fw/UI | no "expected-but-absent" surfacing beyond freshness | **done in tree** (dimmed stale signal cells in LiveCan, explicit "(stale)" on badges) |
| **WP-09** | **High** | backend+UI | **0x621 RT_DIAG_EVENT_RPT not consumed anywhere — precise RT cause (RtHostDriveCmdStale) invisible; the designed "why" channel is dead** | **open** |
| **WP-10** | Medium | frontend | Topbar ESTOP chip uses only CAN-derived `observeEstop`; ignores backend `status.estop` (primary_cause/attribution) → chip and Diagnostics can disagree | **done in tree** (`Topbar.tsx` reconciles `status.estop` primary_cause & summary with CAN fallback) |
| **WP-11** | Medium | firmware | `g_estop_reason` latch: reasons 8/9 never stored (stale-cmd raises diag only); latched 10 never clears while zero-setpoint → 0x210/0x621 disagree | **open** |
| WP-12 | Blocker | deploy | In-tree fixes ready: backend uvicorn restart needed to serve wire_hash `ad2e52…`, firmware reflash needed for physical bench | **pending deploy** |

---

## 5. Parallel-session inventory (already in working tree, uncommitted)

| File | Change |
|---|---|
| `protocol/contracts/rt.yaml` | estop_reason max 7→15; 0x501-high cycle 20→100 ms |
| `protocol/contracts/sys.yaml` | 0x500 cycle 20→200 ms (both buses) |
| `protocol/generated/**` + `baseline-manifest.json` | full regen (new wire hash `ad2e5225…`) |
| `native-test/test/test_generated_codecs.cpp` | codec range tests (0-15) |
| `control-toolkit/backend/.../state/latest.py` | median-of-8 gap rate estimator, 1 ms / 0.25×cycle floor, 5× cap vs expected |
| `control-toolkit/backend/tests/test_latest_rate.py` | new test (burst immunity) |
| `control-toolkit/frontend/src/lib/signals.ts` | `rtStale`, `lastKnownReason*`, freshness-guarded rtState |
| `control-toolkit/frontend/src/lib/ecuPresence.ts` | `presenceFallbacks` + tooltip fallback note |
| `control-toolkit/frontend/src/components/Diagnostics.tsx` | stale RT row, NODE_STATUS block-mask decoder, updated reason map |
| `control-toolkit/frontend/src/components/LiveCan.tsx` | rate display clamp, dimmed stale signal cells |
| `control-toolkit/frontend/src/components/DriveConsole.tsx` | RT mode chip "(stale)" suffix |

---

## 6. Workstreams (remaining work)

### WS-A — Firmware (rt-esp32) — all open

#### A1. WP-01-f + WP-11 — honest 0x210 + consistent reason reporting
1. In the 0x210 block: count `state_rpt_encode_fail++`, log first + every 100th; never
   silently skip a periodic TX.
2. Store reasons for stale-cmd and (if ever raised) egas-mismatch into `g_estop_reason`
   alongside their `diag().raise()` — so 0x210 reason and 0x621 diag_id always agree.
3. Re-examine the latch-clear condition (`main.cpp:1190-1195`): while inhibited
   (no-sys-authority) the reason stays — acceptable — but then 0x210 **must still flow**
   (fixed by A1.1 + widened range).
4. Rebuild + reflash. Verify via serial: `RT_STATE_RPT` ticks at 10 Hz with reason=10 while
   bench estop latched; `RtHostDriveCmdStale` continues on 0x621.

#### A2. WP-02 — 0x7FD High continuity
Reflash with HEAD (heartbeat-ownership commits `a91a529`, `b510184`, `8fb3a8a`); capture
serial during any silence window; if still intermittent, add TX-result instrumentation to
RT_DIAG_RPT reserved bits.

**Acceptance:** 0x7FD High continuous 2 Hz ≥ 10 min; RT-H lamp green (no fallback needed).

#### A3. WP-03 — 0x122 implement-or-reserve (needs product input).

### WS-B — Backend (remaining)

#### B1. WP-09 — Consume 0x621 RT diag events (the designed "why" channel)
1. Store latest 0x621 per `diag_id` (ring of last N events) in the observation pipeline
   (`router.py` on_message hook → new `state/diag_events.py`).
2. Expose in `/control/estop` report: `rt.latest_diag_events[]` (diag_id, label, state,
   occurrence, age) and in the WS heartbeat merge.
3. Map `DiagId` → human label via `diagnostics.yaml` (generated enums already exist in
   `protocol/generated/python`).

**Acceptance:** with RT latched and 0x210 stale, Diagnostics shows
"RT · RtHostDriveCmdStale (active, age 12 s)" — the cause the operator needs, from the
channel designed for it.

#### B2. Deploy (WP-12 backend half)
Restart uvicorn on 8001; confirm `/status` wire_hash = `ad2e5225…`; run
`pytest control-toolkit/backend/tests/test_latest_rate.py`.

### WS-C — Frontend (remaining)

#### C1. WP-10 — Reconcile topbar ESTOP chip with backend report
Topbar (`Topbar.tsx:56-57`) builds `estopObs` from messages only. When `status.estop`
is present and fresh (stream it per rev-1 B2), prefer its `primary_cause`/`summary` for the
chip label/tooltip; keep CAN-derived observation as fallback. Chip and Diagnostics must
never disagree.

#### C2. WP-09 UI half — Diagnostics "RT diag events" section
Render `rt.latest_diag_events` (B1) as a table (diag label, state, occurrences, age);
link it in the ESTOP causes list when `state ∈ {ACTIVE, LATCHED}`.

#### C3. Finish the audit leftovers
- Host lamp tooltip: "Host heartbeat 0x7FC absent while Bench TX off — expected".
- Node lamps for harness-absent ECUs (MTR/SES/SEB): tooltip "not powered on harness" vs fault.
- Ghost 0x210 row in Live CAN (age 22 min): after A1 reflash it disappears; optionally cap
  ghost rows at 5× missing threshold.

### WS-D — Deployment & validation

#### D1. Deploy checklist (order matters)
1. `git add`/review the parallel-session diff (already good; run `npm run lint`, `tsc -b`,
   `pytest` for backend, targeted native-test).
2. Restart backend 8001 → wire_hash `ad2e5225…` served; protocol mismatch chip should stay
   clear (both sides read from backend).
3. Rebuild + reflash RT (A1/A2) → 0x210 returns, reason=10 visible, 0x7FD High continuous.
4. Vite dev server hot-reloads frontend; hard-refresh browser once.

#### D2. Bench scenario (15 min)
Standard loop: all lamps reconcile, 0x210 live both buses, ESTOP chip = backend primary
cause, Live CAN rates sane, HMI AUTO/MANUAL reflected, inject/clear ESTOP cycle, High-bus
unplug 5 s → graceful degrade.

#### D3. Tests
- `test_latest_rate.py` (exists) → run after backend restart.
- `test_generated_codecs.cpp` (updated) → run in native-test.
- e2e additions: RT-H fallback liveness; stale-0x210 ESTOP label; (after B1) diag-event
  visibility.

#### D4. Docs
CHANGELOG (estop_reason 0-15, cycle fixes), `rt-esp32/architecture.md` heartbeat table,
`can-dictionary.md` reason codes 8/9/10, bench bring-up checklist in `run.md`
(reflash check → 0x7FD both buses → 0x210 present → 0x621 flowing).

---

## 7. Sequencing (rev 2)

```
Phase 0 — deploy what exists (no code):
  D1.1 lint/typecheck/tests → D1.2 backend restart → verify rate + hashes + lamps

Phase 1 — small code, high value:
  C1 topbar/backend ESTOP reconciliation
  C3 tooltip leftovers
  B1+C2 0x621 diag-event path (backend store + expose + UI section)

Phase 2 — firmware:
  A1 honest 0x210 + reason-store consistency  → reflash
  A2 0x7FD High continuity verification (same flash)

Phase 3 — closeout:
  A3 0x122 decision, D2 bench scenario, D3 e2e additions, D4 docs
```

Phase 0 alone fixes the reported symptom on the bench (RT-H green via fallback, honest
rates, no stale ESTOP lie). Phase 1 surfaces the true cause channel. Phase 2 removes the
firmware root cause and the heartbeat intermittency.

---

## 8. Acceptance criteria (rev 2)

1. RT powered, Bench TX off: RT-H lamp green (fallback or real 0x7FD), tooltip names the
   satisfying frame.
2. RT latched reason 10: 0x210 live on both buses showing `reason=10 (watchdog)`;
   0x621 shows `RtMtrFbkTimeout`/`RtHostDriveCmdStale` correlated in Diagnostics; no silent
   firmware drops (counter stays 0 when values are in range).
3. 0x7FD High continuous 2 Hz over a 30 min soak.
4. No `observed_rate_hz` > 5× expected anywhere in a 1 h session incl. reconnects.
5. Topbar ESTOP chip label equals backend `primary_cause` whenever the backend report is
   fresh; CAN-derived view only as fallback.
6. Backend + frontend + firmware all on the same wire hash; mismatch chip clear.
7. `pytest`, `npm run lint`, `tsc -b`, targeted native-test, `npm run test:qa` all green.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| Backend restart changes wire hash mid-session | Expected one-time; hello/status auto-sync; document in D4 checklist |
| Widened range masks future >15 misuse | Codec still range-checks; boundary tests exist; 0x621 remains the extensible channel |
| Fallback lamp masks real heartbeat outage | Tooltip shows which frame satisfied presence + primary age when missing |
| 0x621 is event-driven (cycle 0) — can be missed | Backend keeps latest-per-diag_id with age; UI shows age; RT re-reports ACTIVE events periodically (observed ~18 Hz re-raise) |
| Firmware reflash alters bench mid-experiment | Flash inside Phase 2 window; capture `run.log` baseline first |
| Reasons 8/9 stored now (A1.2) change 0x210 content | Purely additive truth; UI reason map already lists 8/9/10 (Diagnostics footer updated) |

---

## 10. Out of scope

- `test_125_auto_to_manual_while_moving`, `test_140_full_acceptance_criteria` native-test
  failures (separate safety-suite workstream; see `run.log`).
- Host 0x7FC absence while Bench TX off — expected behavior, tooltip-only fix.
- SES/SEB/MTR lamp offline — nodes not powered on current harness (tooltip-only fix).

---

*Evidence commands: `GET /api/v1/state|topology|history?limit=4096`, `POST /api/v1/protocol/decode`
(0x620 health, 0x621 diag events), Playwright probe of `ecu-lamp-*` + topbar chips,
`git diff` review of the parallel-session tree.*
