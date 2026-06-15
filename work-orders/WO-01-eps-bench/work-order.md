# WO-01: EPS-C Steering Unit Bench Test

**Group:** CAN — SYNTREE Units
**Shared hardware:** SYNTREE EPS-C, 12V PSU, USB-CAN adapter
**Output:** `docs/eps-c-bench-findings.md`

## Objective

Discover the *actual* behavior of the SYNTREE EPS-C steering unit by commanding it over CAN and observing responses. Do not trust the spec sheet — measure everything.

## Setup

```
Laptop (USB) ──► USB-CAN adapter ──► CAN bus (500 kbit/s, 120Ω terminated)
                                          │
                                    SYNTREE EPS-C
                                    (powered by 12V bench PSU)
```

Tools: `cansend` to transmit, `candump` to capture. Or use `python-can`.

## Tests (do all)

### T1. Boot behavior
Power on EPS-C. Does it transmit anything on its own? Watch `candump` for 30 seconds after power-up. Note every CAN ID and its timing.

### T2. 0x200 command response
Send a 0x200 frame with Angle Mode (control_mode=1), target_angle=0, roll_cnt=0, valid checksum. Does EPS-C respond with 0x201? What's the latency (ms between command and status)?

### T3. Angle sweep
Send 0x200 with target_angle = -780, -400, -200, 0, 200, 400, 780 (0.1°/bit). Read SES_StrAngle from 0x201. Plot commanded vs actual. Measure steady-state error and overshoot.

### T4. Rolling counter check
Send 0x200 with roll_cnt=0,1,2,3,4... Does EPS-C reject (no 0x201) if the counter doesn't increment? Does it accept any change, or must it be sequential?

### T5. Checksum check
Send 0x200 with intentionally wrong checksum. Does EPS-C reject it? Send with correct checksum. Does behavior differ?

### T6. Alignment check
Does EPS-C set SES_INF_Angle_Status=1 automatically on boot? Or does it need a valid 0x200 first? How long from power-on until aligned?

### T7. Timeout behavior
Send 0x200 at 50 Hz for 10 seconds. Then stop. What does EPS-C do?
- Continue sending 0x201? For how long?
- Hold last angle? Center? Freewheel?
- Send any fault indication?

### T8. Slew rate
Send 0x200 with target_speed=50 (°/s), then a large angle change. Measure actual slew rate from 0x201. Repeat with target_speed=100, 200. Does it respect the limit?

## Deliverable

`docs/eps-c-bench-findings.md` containing:
- Raw CAN trace snippets for each test
- Answers to all 8 test questions
- Any surprising behavior not covered by the spec
- Recommended 0x200 frame parameters for production firmware

## Does NOT touch

- `rt-esp32/` — zero files
- `sys-esp32/` — zero files
- `shared/` — zero files
- Only output is one markdown file in `docs/`
