# Observation gaps — ESTOP and similar “not connected well” issues

**Date:** 2026-07-22

## ESTOP (fixed in UI observe path)

### Problem

Several surfaces treated ESTOP as a **single bit**:

| Source | Meaning |
|--------|---------|
| `session.estop_active` | **Host inject latch only** (toolkit latched after Inject ESTOP / Space) |
| `SAFETY_ESTOP` 0x001 High | Physical/virtual **event** on High (DLC=0) |
| `SAFETY_ESTOP` 0x001 Low | Same on Low (must bridge both ways) |
| `SYS_SAFETY_STS` / SYS HB / diag `estop_active` | SYS **reported** ESTOP state |
| `RT_STATE_RPT.mode == ESTOP` | RT **reported** mode |

**Bug:** Topbar used **only** `session.estop_active`. If the bus showed 0x001 or SYS/RT latched ESTOP but the operator never injected from the UI (or Clear was pressed while ECUs still latched), the chip said **Clear** while the vehicle was still in ESTOP — “not connected well.”

Overview partially used SYS signal + latch, but **ignored dual-bus 0x001** and **RT mode**.

### Fix

Shared helper `observeEstop()` in `frontend/src/lib/signals.ts`:

- Host latch  
- Recent `SAFETY_ESTOP` on **high** and **low** (event frames age out quickly; short residual age still counts)  
- SYS `estop_active` on safety / heartbeat / diag  
- RT `mode === ESTOP`  

Topbar + Overview both use it; chip labels distinguish **Host latch**, **Bus High/Low**, **SYS**, **RT ESTOP**, **Latch+bus**.

### Still true (by design)

- Header **Inject ESTOP** is a **protocol test**, not the physical E-stop mushroom.  
- Clear ESTOP clears the **host latch** only; it does not reset hardware.  
- One-shot 0x001 goes **missing** after ~2s of no retransmit — that is event freshness, not “ESTOP cleared on vehicle.”

---

## Similar multi-source gaps (not all fixed)

| Surface | What UI shows | What bus actually has | Risk |
|---------|----------------|------------------------|------|
| **Mode** | Session requested/confirmed only | `HMI_MODE_REQ`, `SYS_MODE_CMD`, `RT_STATE_RPT.mode` | UI “Manual” while RT is ESTOP/AUTO |
| **Power** | Session req/conf only | `HMI_PWR_REQ` + confirmed feedback if any | Same class of desync |
| **TX High / TX Low** (explorer card) | Last seen Host/RT drive frames | Does not show ESTOP suppression of drive | Looks “commanding” while ESTOP active |
| **findMsg(name)** without bus | First match | Dual-bus messages (ESTOP, some bridges) | Wrong bus or single view |
| **Topology** | Heartbeats + SES/SEB/MTR status IDs | No 0x001 event node | ESTOP never a “connected unit” |
| **Monitor live/dead** | Freshness of last frame | Event IDs look “dead” between rare injects | Operator thinks ESTOP path is broken |
| **Overall health** | Now uses multi-source ESTOP | Still weak on mode/power disagreement | Partial |

### Recommended follow-ups

1. `observeMode()` / `observePower()` parallel to `observeEstop()` — session + RT + SYS.  
2. Sidebar TX strip: annotate **suppressed (ESTOP)** when `observeEstop().any`.  
3. Always pass **bus** into `findMsg` for dual-instance IDs.  
4. Event-frame UI: badge **last event** vs continuous **live** for `cycle_ms == 0`.  
5. Keep B15 in bugs.md: never label host latch as physical E-stop hardware.

---

## Quick verify

1. Arm TX → Inject ESTOP → chip shows **Host latch** or **Latch+bus**; both buses show 0x001 briefly.  
2. Clear ESTOP → host latch clear; if SYS still reports estop_active, chip stays **SYS** / **RT ESTOP**.  
3. Real bench with only Low bus-off: may see **Bus High** without Low — that is RT Low path, not a false Clear.
