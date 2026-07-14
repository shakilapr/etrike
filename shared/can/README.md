# CAN Contract Ownership

The project uses controlled ownership, not the assumption that every behavior can be generated.

| Kind | Authority | Examples |
|---|---|---|
| Wire contract | `can_high.yaml`, `can_low.yaml` | ID, DLC, endian, signal location, scale, cycle |
| Generated facts/codecs | `generated/` | DTOs, IDs, masks, hashes and manifests |
| Manual protocol algorithm | `manual-mappings.yaml` and `manual/` | vendor checksum, overlays and interpretation |
| Runtime policy | component `config.h`/policy files | allowed misses, retry and escalation decisions |
| Hardware configuration | component `config.h` and drivers | GPIO, I2C/SPI, oscillator and calibration |

Generated files must never be edited. Application wire literals are allowed only inside generated code, hardware drivers, tests, or an adapter registered by a stable `CAN-MANUAL-*` mapping ID.

## Changing a message

```powershell
python tools/can_change.py inspect SEB_STATUS
python shared/can/generate_code.py
python tools/can_change.py verify SEB_STATUS
```

The inspection result identifies the source, generated DTO, manual behavior, consumers, tests and required builds. If a registered message's wire hash changes, verification fails until its adapter and golden vectors are reviewed and `reviewed_wire_hash` is deliberately refreshed.

Use `python tools/can_change.py list-unregistered` to find remaining direct payload operations. JSON output for automation and LLM clients is available with `--json`.
