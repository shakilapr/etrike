# Protocol Phase 0 — Foundation audit

**Status:** Complete (software track)  
**Date:** 2026-07-16

## What Phase 0 delivers

| Item | Result |
|------|--------|
| Distributed YAML contracts | `protocol/contracts/*.yaml` (no dual `can_high`/`can_low`) |
| Generated Python codecs | `protocol/generated/python/` + custom `protocol/codecs/python/` |
| Generated TypeScript catalog | `protocol/generated/typescript/etrike-protocol.ts` |
| Generated C++ headers | `protocol/generated/cpp/etrike_protocol.hpp` |
| Golden payload vectors | `protocol/vectors/payload-v1.json` |
| Custom codec value vectors | `protocol/vectors/custom-codec-values-v1.json` |
| Semantic hash | `SEMANTIC_HASH` (= `WIRE_HASH`) + `NETWORK_HASH` |
| Drift check | `python -m protocol.tools.protocol generate --check` |
| Targeted checks | `generate python --check`, `generate typescript --check` |

## Critical message audit

All Control Toolkit critical IDs resolve (see `tests/python/test_golden_vectors.py`):

| ID | Name | Buses |
|----|------|-------|
| 0x001 | SAFETY_ESTOP | high, low |
| 0x111 / 0x112 | HMI_MODE_REQ / HMI_PWR_REQ | high (+ low instances) |
| 0x169 | VCU_SES_REQ | low |
| 0x201 | SES_STATUS | low |
| 0x206 | MTR_MOTOR_FBK | low (+ high) |
| 0x300 | HOST_DRIVE_CMD | high |
| 0x310 / 0x311 | STEER_DIAG / BRAKE_DIAG | high |
| 0x600 | SYS_DIAG_RPT | low (+ high) |
| 0x721 | SEB_STATUS | low |
| 0x7B9 | VCU_SEB_REQ | low |
| 0x7FC / 0x7FD / 0x7FE | Host / RT / SYS heartbeats | per contract |

## Forwarding routes vs RT

`network.yaml` routes include ESTOP both ways, SYS safety/diag, MTR feedback, host lights, HMI mode/power. Firmware `can_rx_router` uses generated `is_forwarded_*` helpers aligned to the same contract.

## Hashes

```text
SEMANTIC_HASH  = sha256(canonical wire facts)   # layout/codec/name/DLC/endian
WIRE_HASH      = SEMANTIC_HASH                  # alias for existing consumers
NETWORK_HASH   = sha256(wire + buses + routes + instances)
```

Printed on every `generate` / `validate` / `generate --check`.

## How to verify

```bash
# From monorepo root
python -m protocol.tools.protocol validate
python -m protocol.tools.protocol generate --check
python -m protocol.tools.protocol generate python --check
python -m protocol.tools.protocol generate typescript --check
pytest protocol/tests/python -v
pytest protocol/tests/python/test_golden_vectors.py -v
```

## Out of scope (not Phase 0)

- Transmission policy presentation tags for UI navigation
- Full vehicle ECU simulation
- Control Toolkit backend features beyond importing these codecs
