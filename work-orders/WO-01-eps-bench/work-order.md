# WO-01: SES Steering Unit Bench Test

**Group:** CAN — SES/SEB Units
**Shared hardware:** SES/SEB SES, 12V PSU, USB-CAN adapter
**Output:** `docs/SES-bench-findings.md`

## Objective

Discover the *actual* behavior of the SES/SEB SES steering unit by commanding it over CAN and observing responses. Do not trust the spec sheet — measure everything.

## Setup

```
Laptop (USB) ──► USB-CAN adapter ──► CAN bus (500 kbit/s, 120Ω terminated)
                                          │
                                    SES/SEB SES
                                    (powered by 12V bench PSU)
```

Tools: `cansend` to transmit, `candump` to capture. Or use `python-can`.

## Tests (do all)

### T1. Boot behavior
Power on SES. Does it transmit anything on its own? Watch `candump` for 30 seconds after power-up. Note every CAN ID and its timing.

### T2. 0x200 command response
Send a 0x200 frame with Angle Mode (control_mode=1), target_angle=0, roll_cnt=0, valid checksum. Does SES respond with 0x201? What's the latency (ms between command and status)?

### T3. Angle sweep
Send 0x200 with target_angle = -780, -400, -200, 0, 200, 400, 780 (0.1°/bit). Read SES_StrAngle from 0x201. Plot commanded vs actual. Measure steady-state error and overshoot.

### T4. Rolling counter check
Send 0x200 with roll_cnt=0,1,2,3,4... Does SES reject (no 0x201) if the counter doesn't increment? Does it accept any change, or must it be sequential?

### T5. Checksum check
Send 0x200 with intentionally wrong checksum. Does SES reject it? Send with correct checksum. Does behavior differ?

### T6. Alignment check
Does SES set SES_INF_Angle_Status=1 automatically on boot? Or does it need a valid 0x200 first? How long from power-on until aligned?

### T7. Timeout behavior
Send 0x200 at 50 Hz for 10 seconds. Then stop. What does SES do?
- Continue sending 0x201? For how long?
- Hold last angle? Center? Freewheel?
- Send any fault indication?

### T8. Slew rate
Send 0x200 with target_speed=50 (°/s), then a large angle change. Measure actual slew rate from 0x201. Repeat with target_speed=100, 200. Does it respect the limit?

## Deliverable

`docs/SES-bench-findings.md` containing:
- Raw CAN trace snippets for each test
- Answers to all 8 test questions
- Any surprising behavior not covered by the spec
- Recommended 0x200 frame parameters for production firmware

## Does NOT touch

- `rt-esp32/` — zero files
- `sys-esp32/` — zero files
- `shared/` — zero files
- Only output is one markdown file in `docs/`
