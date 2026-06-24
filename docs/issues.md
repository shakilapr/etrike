# E-Trike — Open Issues (v0.0.4-alpha)

All CRITICAL and HIGH issues from previous reviews are resolved. Remaining items are operational validation and hardware verification.

---

## Bench Testing Required (BLOCKERS for autonomous operation)

| # | Issue | Action |
|:---|:---|:---|
| B1 | Steering angle offset: EPS-C CSV offset=-3000 vs architecture offset=0 | Verify on live CAN bus before autonomous steering |
| B2 | SEB comm-fault behavior: hold vs release on CAN loss | Bench test with pressure gauge. If releases: add NC brake-hold relay |

## Operational

| # | Issue | Action |
|:---|:---|:---|
| O1 | Brake sync failure immobilizes vehicle | Mechanical bypass cable + bench-test SEB unpowered behavior |
| O2 | Steering sync recovery UX | Rider validation of short/long-press START behavior |
| O3 | Wheel encoder loss no ESTOP | Document rationale, add plausibility check |
| O4 | GPIO overlap labeling | Clearly label per-board GPIOs in wiring docs |
| O5 | Integration risks (SPI, I2C, power seq, ADC isolation) | Mutex, startup delay, validation, optoisolation |

---

*Previous resolved issues archived. See git history for full issue tracker.*
