# E-Trike Protocol Package

This package freezes and normalizes the current classic-CAN wire contract. It does not replace `shared/can` or application consumers yet. The migration boundary deliberately preserves every current ID, frame format, DLC, payload layout, cycle, sender, receiver, and transparent route.

## Contract model

- `contracts/network.yaml` owns buses, nodes, route references, and the multi-sender ESTOP definition.
- Sender or vendor-family files own each remaining canonical message exactly once.
- Files use JSON syntax, which is valid YAML 1.2 and allows deterministic, dependency-free tooling.
- A canonical identity is `owner:key`; a runtime identity is `(bus, CAN ID)`.
- Every physical occurrence declares `bus`, `id`, `frame_format`, sender, receivers, cycle, and one of `same_frame`, `regenerated`, or `independent`.
- Every payload selects exactly one `generated`, `profile`, or `custom` strategy. Profile/custom strategies require a versioned implementation/profile ID and vector-set ID.
- `contracts/baseline-manifest.json` is the frozen normalized export of the current high, low, and PWT contracts. Validation fails on any instance drift.

`same_frame` means transparent forwarding with unchanged ID, format, DLC, and bytes. `regenerated` means decode and new emission with independent sequence state. `independent` means bus-local production and sequence state. The current routes are transparent; the schema supports and validates regenerated routes without claiming one exists today.

## Wire compatibility

Ordinary E-Trike and PWT layouts use generated codecs. SES and SEB retain versioned custom compatibility selections so an ordinary generated codec cannot compete with the vendor behavior. Their vectors pin checksum and endian-sensitive bytes.

SES version semantics are explicitly unresolved. `ses:ses_version` supports raw eight-byte access and returns `unsupported_semantics`; no software/hardware version fields are inferred without vendor or known-hardware evidence.

## Generated artifacts

- `generated/discovery.json`: discovery index, routes, layouts, strategies, and instances.
- `generated/capabilities.json`: per-language strategy and semantic support.
- `generated/errors.json`: portable status vocabulary and unsupported capabilities.
- `generated/contract-schema.json`: machine-readable schema artifact.
- `generated/cpp/etrike_protocol.hpp`: C++ compatibility metadata.
- `generated/python/etrike_protocol.py`: generated codecs and custom raw/checksum compatibility.
- `generated/typescript/etrike-protocol.ts`: TypeScript metadata and raw compatibility.

## C++ migration boundary

`protocol/compat/can.hpp` is the non-payload compatibility surface for firmware migration. It
provides canonical IDs, `Mode`/`Gear`, generated route lookups, allocation-free transport frame
conversion, and encode/decode adapters. Application payloads remain the generated types under
`can::gen` or the selected SES/SEB implementations under `can::custom`; the boundary does not
provide handwritten DTO `to_frame`/`from_frame` codecs. `protocol/compat/transport.hpp` is usable
independently in C++11 driver code, while the generated metadata boundary targets the C++17 mode
already configured by ESP32 and STM32 firmware builds.

Generation is deterministic. Verification is read-only and never rewrites an output:

```text
python -m protocol.tools.protocol validate
python -m protocol.tools.protocol generate --check
python -m protocol.tools.protocol inspect 0x210 --bus low
python -m unittest discover -s protocol/tests/python -v
```

`inspect` rejects an ID present on multiple buses unless `--bus` is supplied. To intentionally refresh generated files after a reviewed contract change, run `python -m protocol.tools.protocol generate`, inspect the diff, and rerun read-only verification.

## Vectors

`vectors/payload-v1.json` is language-neutral and covers every canonical message, packed bits, signed widths, endian-sensitive payloads, constants, vendor checksums, DLC errors, and standard/extended format errors. `vectors/sequences-v1.json` is separate from payload codecs and covers wrap, duplicate, gap, reorder, frozen producer, recovery, session reset, and independent same-ID state on high and low buses.
