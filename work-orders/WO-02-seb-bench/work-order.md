# WO-02: SEB Brake Unit Bench Test

**Group:** CAN — SYNTREE Units
**Shared hardware:** SYNTREE SEB, 12V PSU, USB-CAN adapter, pressure gauge (if available)
**Output:** `docs/seb-bench-findings.md`

## Objective

Discover the *actual* behavior of the SYNTREE SEB electro-hydraulic brake unit. Every question below affects safety-critical firmware design.

## Setup

```
Laptop (USB) ──► USB-CAN adapter ──► CAN bus (500 kbit/s, 120Ω terminated)
                                          │
                                    SYNTREE SEB
                                    (powered by 12V bench PSU)
```

## Tests

### T1. Boot behavior
Power on SEB. Watch `candump` for 30s. Does it transmit 0x721 on its own? What's in the first frame?

### T2. Stroke Mode command
Send 0x720 with control_mode=1 (Stroke), stroke_req = 600 (0mm), 700, 800, 900, 1000, 1140 (27mm max). Read SEB_Stroke_Value from 0x721. Verify scale: is it actually raw = (mm + 30) / 0.05?

### T3. Pressure Mode — scale verification
Send 0x720 with control_mode=2 (Pressure), pressure_req = 0, 20, 40, 60, 80, 100. Read VCU_SEB_Pre_Value from 0x721. Verify: 1 bit = 0.05 MPa? Is pressure_req actually u8 at byte 4, or is it u16 spanning bytes 4-5?

### T4. Rolling counter + checksum
Same tests as EPS-C: does SEB require sequential counter? Does it reject bad checksum?

### T5. Mode switching mid-operation
Send Stroke Mode at stroke=900 (15mm). Then switch to Pressure Mode at pressure=50 (2.5 MPa). What does SEB do? Does it hold stroke position during the mode switch? Any pressure spike or drop?

### T6. Timeout behavior
Send 0x720 at 50 Hz. Stop. Does SEB hold pressure? Release? How long until it faults?

### T7. Unpowered fail-safe — CRITICAL
Cut 12V power to SEB while it's holding pressure. Does it:
- (a) Release pressure → vehicle can roll → mechanical bypass optional
- (b) Hold pressure → vehicle locked → mechanical bypass MANDATORY

This single answer determines whether the trike needs a physical brake cable.

### T8. Alignment sequence
Does SEB set alignment bit in 0x721 automatically, or after receiving valid 0x720? How long from power-on to aligned?

## Deliverable

`docs/seb-bench-findings.md` with all 8 answers, raw traces, and pressure gauge readings if available.

## Does NOT touch

Any source file. Only `docs/seb-bench-findings.md`.
