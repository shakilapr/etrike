# CAN Maintainability Work Plan and Checklist

> **Superseded target:** This checklist records work completed around the transitional mapping registry. Do not extend the registry into the final architecture. The replacement target and staged migration are defined in [`protocol-architecture-migration-plan.md`](protocol-architecture-migration-plan.md).

Goal: every CAN change must be automatically discoverable even when its implementation cannot be fully generated.

The governing rule is: every protocol value is generated, centrally named, or registered as a tested manual mapping. Generated files are never edited manually.

## Ownership and documentation

- [x] Document wire-contract, protocol-algorithm, runtime-policy, hardware and manual-exception ownership.
- [x] Document authoritative files and regeneration/verification commands.
- [x] Require mapping IDs for handwritten wire behavior.
- [x] Prohibit unexplained IDs, DLCs, offsets and masks in application logic.

## Generated metadata

- [x] Generate message ID, DLC, cycle, frame format and bus-instance metadata.
- [x] Generate signal byte, bit, width, mask, range, enum, scaling and counter metadata.
- [x] Generate per-message wire hashes.
- [x] Generate `change_impact.json` with consumers, tests and build targets.
- [x] Test duplicate multi-bus instances and counter metadata.

## Manual mapping register and adapters

- [x] Add and schema-validate `shared/can/manual-mappings.yaml`.
- [x] Register SES status/test/error/version and command mappings.
- [x] Register SEB status/test/error/version and command mappings.
- [ ] Register manufacturer-specific PWT behavior where applicable.
- [x] Reject duplicate IDs, missing files, stale hashes and mappings without tests.
- [ ] Isolate checksum and handwritten payload manipulation in `shared/can/manual/` adapters.
- [x] Require exact ID, frame type and DLC; leave output unchanged on failure.
- [x] Validate checksum/constants before application fault processing.
- [ ] Replace inline RT/SYS vendor parsing with adapters.

## Ordinary codec migration

- [x] Migrate remaining Jetson application-message decoders.
- [x] Generate the Jetson shutdown command.
- [x] Replace ordinary application IDs, DLCs, masks and payload indexing.
- [x] Replace manually created ESTOP frames in the Jetson boundary.
- [ ] Build Jetson in Linux/ROS CI.

## Runtime and hardware configuration

- [ ] Inventory timeouts, retries, queue depths, logging intervals and escalation thresholds.
- [x] Centralize heartbeat/staleness relationships and derive their wire cycles from generated metadata.
- [x] Keep allowed misses and escalation behavior explicit application policy.
- [ ] Inventory and centrally name GPIO, bus timing, I2C/SPI, calibration and task configuration.
- [ ] Document hardware values that require manual calibration or datasheet review.

## Change discovery tooling

- [x] Implement `inspect NAME|ID` with human and JSON output.
- [x] Implement `affected FILE`, `list-manual`, `list-unregistered` and `verify`.
- [x] Show source, generated facts, manual behavior, consumers, tests, builds and exact commands.
- [x] Return nonzero status for stale or incomplete mappings.

## Tests and CI

- [ ] Add independent valid, boundary, signed, endian and roundtrip vectors.
- [x] Add invalid DLC/enum/constant/checksum and unchanged-output tests.
- [ ] Add frozen and wrapping counter sequences.
- [x] Verify generated artifacts and mappings in CI.
- [x] Reject unregistered direct payload access, literal application IDs and new legacy codec use.
- [x] Build RT, SYS, MTR and PWT; keep Jetson Linux/ROS compilation as the remaining environment-specific build.

## Legacy retirement and backend visibility

- [ ] Inventory and allowlist existing legacy codec consumers.
- [ ] Prevent new legacy uses, migrate registered consumers, then remove obsolete DTOs.
- [ ] Expose protocol hashes, error catalog, aggregated errors, freshness and mapping metadata to UI/LLM clients.
- [ ] Bound raw high-rate event retention and use rate-limited summaries.

## Definition of done

- [ ] Every wire value is generated or registered as a manual mapping.
- [x] Every registered manual mapping has a stable ID, reviewed hash and golden-vector test entry.
- [x] One command reports the complete impact of changing any message.
- [x] CI catches stale generated output, stale manual mappings and unexplained protocol literals.
- [ ] Runtime policy and hardware configuration have clear owners.
- [ ] All available schema, native, firmware, host and backend verification passes.

## Standard change workflow

1. Run `python tools/can_change.py inspect MESSAGE_NAME`.
2. Edit the authoritative YAML or the explicitly owned runtime/hardware configuration.
3. Run `python shared/can/generate_code.py`.
4. Review every manual mapping reported by the inspection tool.
5. Update independent vectors if wire bytes changed.
6. Run `python tools/can_change.py verify MESSAGE_NAME`.
7. Build the affected targets listed by the tool.
