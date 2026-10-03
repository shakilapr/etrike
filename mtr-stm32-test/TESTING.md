# MTR Board Bring-Up & Test Checklist

Step-by-step bench procedure for the MTR STM32G431 board. Work through the
stages in order — hardware first, then CAN — ticking items as you go. One
bench session = one row in the Session Log at the bottom.

> **Rules of the bench**
> - Judge LED verdicts **3+ seconds after power-on** (boot is fast; dark only
>   counts as Bus-Off after that).
> - The Canalyst-II must be in **Active mode** (it must ACK frames, not
>   listen-only/monitor).
> - Termination: measure `CANH ↔ CANL` with power OFF — target **~60 Ω**
>   (two 120 Ω terminators at the bus ends; MTR itself is a middle tap).

---

## 0. Bench Setup (prerequisites)

- [ ] Board powered — 3.3 V rail measured at the transceiver/module VCC
- [ ] DFU flashing works from this machine
      ```bash
      pio run -e vehicle -t upload
      ```
- [ ] Canalyst-II attached to the Low bus, **Active/ACK mode**
- [ ] Termination ≈ 60 Ω measured (power OFF)
- [ ] RM node on the Low bus (traffic source for RX tests)

**Stage 0 result:** PASS / FAIL — notes:

---

## 1. Hardware Test — relays, DAC, LED (`env:vehicle`, default)

Upload the default environment and watch/measure. No CAN needed.

```bash
pio run -e vehicle -t upload
```

| # | Check | Pass criterion | ✓ |
|---|---|---|---|
| 1.1 | Ignition relay (PA4) | Audible click, 2 s ON / 1 s OFF cycle | [ ] |
| 1.2 | Drive relay (PA2) | Clicks in its slot — **never** together with Reverse | [ ] |
| 1.3 | Reverse relay (PA0) | Clicks in its slot — mutual exclusion holds | [ ] |
| 1.4 | Status LED (PC6) | Lights while a relay is energized, off in pauses | [ ] |
| 1.5 | DAC throttle output | Sine sweep 0 → 2.4 V → 0 V over 30 s (meter/scope on MCP4725 out) | [ ] |

> A running sweep also proves the MCP4725 **ACKed on I2C** (the driver probes
> 0x60/0x61/0x62). No sweep = DAC absent, wiring open, or I2C fault.

**Stage 1 result:** PASS / FAIL — what failed:

> If anything here fails, **fix hardware before continuing** — the CAN stages
> assume working relays/DAC. The standalone relay/DAC behavior has no CAN
> dependency, so a failure is pure board-level (driver stage, wiring, MCU pin).

---

## 2. CAN RX Test (`env:can_rx`)

Passive listener — verifies PA11 / transceiver RX stage / RX wiring.

```bash
pio run -e can_rx -t upload
```

Generate traffic (RM sending commands, or Canalyst-II transmitting frames).

| Observation | Meaning | ✓ |
|---|---|---|
| **LED solid ON** | Frames arriving within the last 500 ms → **RX path OK** | [ ] |
| LED slow (1 Hz) | FDCAN listening but **no frames** → transceiver RX stage or PA11 wiring | [ ] |
| LED fast (5 Hz) | FDCAN init failure → firmware/config (report it) | [ ] |

**Stage 2 result:** PASS / FAIL — notes:

---

## 3. CAN TX Test — HSI clock, current production setup (`env:can_tx`)

One `0x206` frame per second (Classic CAN 2.0A, production-identical bit
timing: 500 kbit/s, 87.5 % sample point). Verdict on the PC6 LED; judge
**3+ seconds** after power-on. Canalyst-II should show `0x206` @ 1 Hz on
success.

```bash
pio run -e can_tx -t upload
```

| Observation | Meaning | ✓ |
|---|---|---|
| **LED solid ON** | Frame transmitted (`TXBRP` drained, `TEC` flat, `LEC` clean) → **TX path proven** | [ ] |
| LED slow (1 Hz) | `LEC = 3` — ACK error: frame reached the bus, nobody ACKed (partner in listen-only? termination?) | [ ] |
| LED fast (5 Hz) | Bus-level error (`LEC 1/2/4/5/6`, stuck pending, or init fail) → PA12→TXD wire, transceiver TX stage, USB-D+ loading on PA12 | [ ] |
| LED dark | Bus-Off latched → persistent hard fault on the TX path | [ ] |

**Stage 3 result:** PASS / FAIL — observed LED + notes:

---

## 4. CAN Clock Experiment — HSI vs HSE (`env:can_tx_hse` / `env:can_rx_hse`)

Same tests as stages 2–3, but the FDCAN kernel clock comes from the external
8 MHz crystal instead of the internal HSI. Wire timing identical — only clock
accuracy differs.

```bash
pio run -e can_tx_hse -t upload   # TX verdict, crystal clock
pio run -e can_rx_hse -t upload   # RX activity, crystal clock
```

| # | Check | ✓ |
|---|---|---|
| 4.1 | `can_tx_hse` LED verdict: ___________ (solid/slow/fast/dark) | [ ] |
| 4.2 | `can_rx_hse` LED verdict: ___________ (traffic/listening/fail) | [ ] |

**Decision rule:**
- Both HSI and HSE variants work → adopt **HSE** in production (ST
  recommendation; robust for the real vehicle).
- HSE fails (init failure) but HSI works → crystal missing/faulty on this
  board (verify it is an 8 MHz part).
- HSI fails but HSE works → clock-integrity issue found; adopt HSE.

**Stage 4 result:** PASS / FAIL — adopted clock choice:

---

## 5. Decision Gate

| Situation | Next action | ✓ |
|---|---|---|
| Any Stage 1 FAIL | Fix board hardware; re-run Stage 1 only | [ ] |
| Stage 2 deaf (no RX) | Transceiver RX stage / PA11 wiring; re-run Stage 2 | [ ] |
| Stage 3 solid ON (TX proven) | Re-enable production publishing: `mtr-stm32/src/config.h` → `kEnableCanBroadcast = true`; flash `mtr-stm32` (`pio run -e vehicle -t upload`); soak 5 min — no relay dropouts, Canalyst-II sees `0x206` @ 50 Hz, SYS sees the ESTOP-ACK | [ ] |
| Stage 3 slow/fast/dark | Hardware fix list: PA12→TXD continuity · transceiver TX stage & mode pin · WeAct USB-D+ pull-up network on PA12 · termination · ACK partner mode | [ ] |

---

## 6. Restore Production Firmware

Always end a bench session with the vehicle firmware on the board:

```bash
cd ..\mtr-stm32
pio run -e vehicle -t upload
```

- [ ] Production firmware restored and relays confirmed OFF at power-up

---

## Session Log

| Date | Stage(s) run | Result | Notes |
|------|--------------|--------|-------|
|      |              |        |       |
