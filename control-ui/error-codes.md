# Control UI Error Codes and Logging Contract

**Purpose:** Provide one stable error/event vocabulary for backend logs, API responses, recordings, test evidence, React, LLM tools, and optional terminal clients.

These codes describe the Control UI and its test infrastructure. RT, SYS, MTR, PWT, EPS-C, and SEB diagnostic flags remain ECU-reported data and are not remapped into fake Control UI failures.

## 1. Error identity and standards

```text
<domain>.<condition>
```

Examples:

```text
adapter.device_removed
protocol.checksum_invalid
test.evidence_incomplete
```

Every condition has both a fixed catalog ID and a readable symbolic code. Both are mandatory in APIs, logs, recordings, tests, and UI. The catalog ID is short and immutable; the symbolic code is directly understandable and searchable.

```text
catalog_id   CUI-ADP-007
code         adapter.device_removed
message      CANalyst-II disappeared during adapter epoch 4.
event_id     evt_01J...   unique occurrence of this condition
```

`catalog_id` and `code` describe the condition and never change meaning. `message` describes this occurrence and may include safe contextual values. `event_id` identifies one raised/updated/recovered event occurrence and links it to evidence.

The catalog tables retain the uppercase condition beside the catalog ID for reviewability. The registry stores the full symbolic code explicitly; it is not reconstructed by clients. Domain names are `system`, `logging`, `api`, `adapter`, `can`, `pipeline`, `protocol`, `tx`, `ecu`, `test`, `recording`, `replay`, `stream`, `ui`, and `projection`.

Use established identifiers at the boundary where they apply instead of inventing replacements:

| Boundary | Standard representation |
|---|---|
| HTTP request failure | HTTP status plus RFC 9457 `application/problem+json` |
| Logs/traces | OpenTelemetry-compatible `error.type`; exception logs also use `exception.type`, `exception.message`, and `exception.stacktrace` |
| CAN controller | Preserve reported states such as `error_active`, `error_warning`, `error_passive`, and `bus_off`, and native SocketCAN error classes where available |
| UDS ECU diagnostic | Preserve the ECU's DTC and negative-response code; only use these when the ECU actually implements UDS |
| J1939 diagnostic | Preserve SPN/FMI; only use these on a J1939 network |
| Driver/OS/library | Preserve the native error number/type in `native_error`, while also assigning one stable Control UI `error.type` |

Custom E-Trike YAML faults are not converted into invented DTCs, SPN/FMIs, or generic CAN controller errors. A CANalyst-II limitation also must not be presented as a standard controller state that it did not report.

For an HTTP failure, the symbolic code is both the `code` extension and the final portion of a stable problem-type URI. For non-HTTP events, the same value is used as `code` and `error.type`.

```json
{
  "type": "https://etrike.local/problems/adapter/device-removed",
  "title": "CAN adapter removed",
  "status": 503,
  "detail": "The selected CANalyst-II disappeared during adapter epoch 4.",
  "instance": "/api/v1/events/evt_456",
  "code": "adapter.device_removed",
  "catalog_id": "CUI-ADP-007",
  "severity": "ERROR",
  "retryable": true,
  "event_id": "evt_456",
  "adapter_epoch": 4,
  "evidence_refs": ["evt_456"]
}
```

The problem-type base URI is configuration, not an assumption that the bench is Internet-connected. It should resolve to local documentation when practical.

Rules:

- Symbolic codes are stable and never reused for a different meaning.
- Names follow `lowercase_domain.lowercase_condition`; words inside either part use underscores.
- Dynamic values do not appear inside the code; they belong in structured fields.
- The same condition uses the same code in logs, API errors, UI notices, and evidence.
- Each condition has an `event_state`: `raised`, `updated`, or `recovered`.
- Recovery normally uses the original code with `event_state=recovered`, not a second recovery code.
- Severity and test verdict are separate. An infrastructure `ERROR` may make a test `Inconclusive`, while valid contradictory ECU evidence may make a test `Fail`.
- Unsupported adapter evidence is `unknown`, not an invented success or zero-valued metric.
- Every error reports the condition the backend observed. It must not claim an unobserved electrical cause, physical sender identity, frame delivery, or ECU acceptance.

### 1.1 Evidence basis and certainty

`evidence_basis` describes certainty about the reported condition, not certainty about its root cause.

| Basis | Backend guarantee | Examples |
|---|---|---|
| `observed` | The backend directly observed the application, OS, USB driver, adapter, queue, or stream condition. | An adapter open call failed; the router queue overflowed; a WebSocket sequence gap occurred. |
| `derived` | The backend deterministically evaluated retained evidence against the active configuration, YAML contract, and monotonic deadlines. | A received DLC differs from YAML; a periodic message missed its deadline; a checksum does not validate. |
| `reported` | The adapter or ECU reported the condition. The backend preserves the report but does not independently prove the physical cause. | Adapter-reported bus-off; ECU-reported diagnostic fault. |
| `inferred` | Evidence indicates a possible cause but cannot prove it. These conditions use names such as `*_SUSPECT`; they never use a definitive physical-failure name. | Bus-specific IDs conflict with the configured channel mapping. |
| `unknown` | The required evidence is unavailable or unsupported. | CANalyst-II does not expose TEC/REC, bus-off, or hardware-overflow evidence. |

The backend can be certain that `CHANNEL_QUIET` means no raw frames arrived during the configured window. It cannot conclude that a CAN wire is broken. A quiet channel can also result from an unpowered ECU, wrong bitrate, missing termination, incorrect mapping, or an intentionally silent bus.

### 1.2 Conditions the backend must not diagnose as facts

The following physical causes require an electrical measurement or controlled external test. The backend may show the supporting evidence and a guided check, but must not emit a definitive error code for the cause:

| Do not diagnose as fact | Backend may report instead |
|---|---|
| Broken, open, shorted, or swapped CANH/CANL wire | `CUI-CAN-010 CHANNEL_QUIET`, USB/adapter errors, or a mapping suspect |
| Missing/incorrect termination or common-ground fault | Quiet/invalid traffic or adapter-reported error evidence when available |
| Actual electrical sender of an ordinary CAN ID | YAML expected sender and observed bus/ID only |
| Physical ECU absent, unpowered, or defective | Required heartbeat/message evidence missing |
| Frame delivered to or accepted by an ECU | Adapter submission and separately observed expected response |
| Wire CRC error for a discarded frame | Adapter error evidence only when the adapter exposes it |
| Healthy bus state from unsupported TEC/REC, bus-off, or overflow metrics | `CUI-ADP-011 CAPABILITY_UNKNOWN` |

## 2. Severity

| Level | Meaning | Default behavior |
|---|---|---|
| `DEBUG` | Detailed trace useful during development | Not shown by default; rate-limited |
| `INFO` | Expected lifecycle or state transition | Persist important transitions |
| `WARN` | Degraded behavior that may recover or may not affect evidence | Visible diagnostic event |
| `ERROR` | Operation failed or evidence may be invalid | API/test operation reports failure or Inconclusive as appropriate |
| `CRITICAL` | Backend cannot safely continue the current test/session | Stop affected jobs, preserve evidence, require intervention |

`CRITICAL` means critical to bench-test integrity, not a production vehicle safety classification.

### 2.1 HTTP status is not the test verdict

HTTP status describes whether the API request was accepted and processed. A completed bench test with verdict `Fail` or `Inconclusive` is still a successful resource operation and normally returns `200` (or `202` while running). Do not turn an expected test outcome into a `4xx` or `5xx` response.

Recommended request-failure mappings are `400` malformed request, `401` unauthenticated, `403` capability denied, `404` resource not found, `409` ownership/revision conflict, `412` protocol or manifest precondition failed, `422` semantically invalid values, `429` rate limited, `503` adapter/backend unavailable, and `504` an infrastructure deadline failure. A normal bounded wait that expires returns `200` with `disposition: timeout`; it is not a gateway error.

### 2.2 Standards references

- [RFC 9457: Problem Details for HTTP APIs](https://www.rfc-editor.org/rfc/rfc9457.html)
- [OpenTelemetry: Recording errors](https://opentelemetry.io/docs/specs/semconv/general/recording-errors/)
- [OpenTelemetry: Exception logs](https://opentelemetry.io/docs/specs/semconv/exceptions/exceptions-logs/)
- [Linux SocketCAN error-message frames](https://docs.kernel.org/networking/can.html#error-message-frames)

## 3. Required structured fields

Every persisted event contains:

```text
schema_version
event_id
catalog_id               fixed condition ID, for example CUI-ADP-007
code                     readable condition name, for example adapter.device_removed
severity
event_state
evidence_basis             observed, derived, reported, inferred, or unknown
message                  contextual human explanation for this occurrence
wall_time_utc
monotonic_time_us
process_instance_id
protocol_hash
request_id                when caused by an API request
client_id / actor_id      when known
session_id / session_revision
job_id / test_id / test_step
adapter_id / adapter_epoch
channel / bus
can_id / message / signal when applicable
source / provenance
expected / actual
queue_depth / high_water / dropped_count when applicable
exception_type
error.type               same value as code for an error condition
native_error             bounded native driver/OS/library details when present
condition_instance_id    one active fault episode from raise through recovery
occurrence_count         exact count within this episode
first_occurrence_time
last_occurrence_time
summary_window_ms        for an aggregated update
aggregated_count         occurrences represented by this emitted record
representative_samples   bounded first/last/worst evidence references
aggregation_key          registry-approved low-cardinality condition scope
evidence_refs
context                   bounded structured object
```

Events also carry `origin_service`, `detector`, and optional `cause_event_id`/`root_event_id`. These fields let clients distinguish the primary failure from downstream consequences.

Secrets, capability tokens, USB handles, and unrestricted payload dumps are never logged. Raw CAN bytes may be linked as evidence or included in bounded CAN-specific context.

## 4. General and configuration

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-GEN-001` | CRITICAL | `UNHANDLED_EXCEPTION` | An exception escaped its owning service boundary |
| `CUI-GEN-002` | ERROR | `CONFIG_INVALID` | Application/bench configuration failed validation |
| `CUI-GEN-003` | ERROR | `DEPENDENCY_UNAVAILABLE` | Required library, driver, or external executable is unavailable |
| `CUI-GEN-004` | ERROR | `STARTUP_FAILED` | Backend startup could not reach ready state |
| `CUI-GEN-005` | ERROR | `SHUTDOWN_INCOMPLETE` | Shutdown timed out or left an owned resource unresolved |
| `CUI-GEN-006` | WARN | `CLOCK_DISCONTINUITY` | Host monotonic/session time mapping became discontinuous |
| `CUI-GEN-007` | WARN | `RESOURCE_EXHAUSTED` | Memory, worker, file descriptor, or other bounded resource was exhausted |
| `CUI-GEN-008` | ERROR | `INVARIANT_VIOLATION` | Internal state violated a condition that should be impossible |

### 4.1 Logging infrastructure

| Catalog ID | Default | Symbolic code | Meaning |
|---|---:|---|---|
| `CUI-LOG-001` | ERROR | `logging.pipeline_overloaded` | The bounded operational-log pipeline exhausted its reserved capacity and aggregated or dropped output records |
| `CUI-LOG-002` | ERROR | `logging.sink_write_failed` | A console, file, export, or telemetry sink failed to accept a log record |
| `CUI-LOG-003` | WARN | `logging.cardinality_limit_reached` | A detector exceeded its bounded active aggregation keys and collapsed excess keys into an overflow group |

## 5. API, contract, and capabilities

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-API-001` | WARN | `REQUEST_INVALID` | Request failed schema or semantic validation |
| `CUI-API-002` | ERROR | `API_VERSION_UNSUPPORTED` | Client and backend API versions are incompatible |
| `CUI-API-003` | ERROR | `PROTOCOL_HASH_MISMATCH` | Client/backend/generated protocol semantics differ |
| `CUI-API-004` | WARN | `CAPABILITY_DENIED` | Session/client lacks the required supported capability |
| `CUI-API-005` | WARN | `REQUEST_CONFLICT` | Request conflicts with current resource state |
| `CUI-API-006` | WARN | `IDEMPOTENCY_CONFLICT` | An idempotency key was reused with different request content |
| `CUI-API-007` | WARN | `SESSION_REVISION_CONFLICT` | Request used a stale expected session revision |
| `CUI-API-008` | WARN | `REQUEST_DEADLINE_EXCEEDED` | Request deadline expired before acceptance/completion |
| `CUI-API-009` | WARN | `RATE_LIMITED` | Client exceeded a bounded API operation rate |
| `CUI-API-010` | INFO | `CLIENT_DISCONNECTED` | API/WebSocket client disconnected |
| `CUI-API-011` | ERROR | `RESPONSE_SERIALIZATION_FAILED` | Valid result could not be encoded into the API contract |
| `CUI-API-012` | ERROR | `GENERATED_CLIENT_DRIFT` | Generated client/tool schema is stale relative to OpenAPI |

## 6. Adapter and USB transport

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-ADP-001` | WARN | `DEVICE_NOT_FOUND` | Configured CANalyst-II was not discovered |
| `CUI-ADP-002` | ERROR | `OPEN_FAILED` | Adapter open/claim operation failed |
| `CUI-ADP-003` | ERROR | `CONFIGURATION_FAILED` | Adapter could not apply requested channels/bitrates/mode |
| `CUI-ADP-004` | ERROR | `RECEIVE_WORKER_FAILED` | Receive worker exited or raised unexpectedly |
| `CUI-ADP-005` | WARN | `WORKER_HEARTBEAT_MISSED` | Adapter worker supervision heartbeat exceeded its deadline |
| `CUI-ADP-006` | ERROR | `USB_IO_FAILED` | USB read/write/control operation failed |
| `CUI-ADP-007` | ERROR | `DEVICE_REMOVED` | Selected physical adapter disappeared during an active epoch |
| `CUI-ADP-008` | WARN | `CLOSE_FAILED` | Best-effort adapter close/shutdown failed |
| `CUI-ADP-009` | WARN | `RECONNECT_FAILED` | A visible reconnect attempt failed |
| `CUI-ADP-010` | WARN | `IDENTITY_CHANGED` | Reopened device identity does not match the selected adapter |
| `CUI-ADP-011` | WARN | `CAPABILITY_UNKNOWN` | A requested adapter metric/capability is unsupported or unverified |
| `CUI-ADP-012` | ERROR | `CHARACTERIZATION_OUTDATED` | Driver/adapter/USB fingerprint changed since validation |
| `CUI-ADP-013` | INFO | `TIMESTAMP_WRAP` | Device timestamp wrapped and was mapped into a new segment |
| `CUI-ADP-014` | WARN | `TIMESTAMP_RESET` | Device timestamp reset unexpectedly within an adapter session |
| `CUI-ADP-015` | WARN | `RX_POLL_DELAY_EXCEEDED` | Measured adapter polling delay exceeded the characterized limit |
| `CUI-ADP-016` | ERROR | `CHARACTERIZATION_REQUIRED` | No approved characterization record exists for the current adapter/driver/OS/USB fingerprint; physical formal tests are blocked |
| `CUI-ADP-017` | WARN | `PRESENCE_PROBE_UNRELIABLE` | Device-presence probing failed or could not be proven safe for the open adapter; I/O and worker evidence remain the loss detectors |
| `CUI-ADP-018` | INFO | `ADAPTER_EPOCH_CHANGED` | Adapter reopen created a new epoch and invalidated prior adapter-bound state |

## 7. CAN channel and transport evidence

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-CAN-001` | ERROR | `CHANNEL_OPEN_FAILED` | High or Low channel failed to open |
| `CUI-CAN-002` | WARN | `CHANNEL_MAPPING_SUSPECT` | Bus-specific traffic conflicts with configured mapping; this does not prove wiring or channel assignment |
| `CUI-CAN-003` | ERROR | `BITRATE_CONFIGURATION_FAILED` | Requested channel bitrate was not applied |
| `CUI-CAN-004` | ERROR | `RX_FAILED` | Channel receive operation failed |
| `CUI-CAN-005` | ERROR | `TX_SUBMIT_FAILED` | Frame could not be submitted to the adapter library |
| `CUI-CAN-006` | WARN | `TX_BACKLOG_TIMEOUT` | Adapter TX queue did not accept work within deadline |
| `CUI-CAN-007` | ERROR | `BUS_OFF_REPORTED` | Adapter reported bus-off where supported |
| `CUI-CAN-008` | WARN | `ERROR_PASSIVE_REPORTED` | Adapter reported error-passive where supported |
| `CUI-CAN-009` | ERROR | `HARDWARE_RX_OVERFLOW` | Adapter reported hardware receive overflow |
| `CUI-CAN-010` | INFO | `CHANNEL_QUIET` | No raw traffic observed for the activity window; this is not adapter loss or proof of a wiring fault |
| `CUI-CAN-011` | WARN | `WRONG_BUS_FRAME` | Known message was observed on a bus disallowed by YAML; this does not prove the physical sender or wiring cause |
| `CUI-CAN-012` | INFO | `CROSS_CHANNEL_ORDER_UNCERTAIN` | Cross-bus arrival order cannot be treated as wire order |
| `CUI-CAN-013` | WARN | `UNEXPECTED_ERROR_FRAME` | Adapter exposed a CAN error frame/event |
| `CUI-CAN-014` | WARN | `TX_ECHO_UNAVAILABLE` | Adapter cannot distinguish bus-observed TX from submitted TX |
| `CUI-CAN-015` | ERROR | `CHANNEL_MAPPING_UNVERIFIED` | No current characterization record verifies the configured channel-to-bus mapping; physical TX is blocked |

Do not emit `CUI-CAN-007` through `009` when CANalyst-II cannot provide that evidence. Report the capability as unknown instead.

`CUI-CAN-002` is `inferred`. Codes `001` through `006` and `010` through `015` are `observed` or `derived` according to their retained context. Codes `007` through `009` are `reported` because the adapter, rather than the backend, supplies the controller-state evidence. `CUI-ADP-016` and `CUI-CAN-015` are `derived` from the current characterization registry; `CUI-ADP-017` and `018` are `observed`.

## 8. Receive pipeline and overload

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-RX-001` | ERROR | `ROUTER_QUEUE_OVERFLOW` | Raw router queue dropped or rejected an envelope |
| `CUI-RX-002` | ERROR | `DECODE_QUEUE_OVERFLOW` | Decode/validation input could not retain all frames |
| `CUI-RX-003` | WARN | `FRAME_SEQUENCE_GAP` | Internal/channel sequence proves one or more envelopes missing |
| `CUI-RX-004` | WARN | `DUPLICATE_ENVELOPE` | Same transport envelope sequence was received twice |
| `CUI-RX-005` | WARN | `TIMESTAMP_NON_MONOTONIC` | Timestamp moved backward inside a channel/mapping segment |
| `CUI-RX-006` | INFO | `STALE_EPOCH_FRAME` | Buffered frame from an obsolete adapter epoch was rejected |
| `CUI-RX-007` | ERROR | `ASSERTION_INPUT_LOSS` | Active assertions did not receive complete relevant observations |
| `CUI-RX-008` | WARN | `WORKLOAD_BUDGET_EXCEEDED` | Declared tested frame/processing workload was exceeded |
| `CUI-RX-009` | ERROR | `CRITICAL_EVENT_LOSS` | Critical internal event could not be retained/delivered |
| `CUI-RX-010` | ERROR | `ROUTER_WORKER_FAILED` | The router task exited or raised unexpectedly |
| `CUI-RX-011` | WARN | `ROUTER_LATENCY_EXCEEDED` | Measured RX queue age or router processing latency exceeded its configured budget |

## 9. Protocol, decode, and integrity

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-PRO-001` | INFO | `UNKNOWN_MESSAGE` | CAN ID is not defined for the observed bus |
| `CUI-PRO-002` | WARN | `DLC_INVALID` | Received DLC differs from the YAML definition |
| `CUI-PRO-003` | WARN | `DECODE_FAILED` | Raw frame could not be decoded into declared signals |
| `CUI-PRO-004` | ERROR | `ENCODE_FAILED` | Requested semantic values could not be encoded |
| `CUI-PRO-005` | WARN | `SIGNAL_OUT_OF_RANGE` | Received/requested signal exceeds declared bounds |
| `CUI-PRO-006` | WARN | `ENUM_INVALID` | Raw value is not a declared enum member |
| `CUI-PRO-007` | WARN | `CHECKSUM_INVALID` | Application checksum/XOR/CRC validation failed |
| `CUI-PRO-008` | WARN | `COUNTER_DISCONTINUITY` | Rolling/alive counter skipped, repeated, or moved unexpectedly; this does not attribute the cause to the sender because capture loss or reordering can produce the same evidence |
| `CUI-PRO-009` | WARN | `COUNTER_FROZEN` | Frames arrive while the required alive counter does not advance |
| `CUI-PRO-010` | WARN | `REQUIRED_FLAG_INVALID` | Mandatory enable/security flag has an invalid value |
| `CUI-PRO-011` | WARN | `MULTIPLEXOR_INVALID` | Multiplexer/overlap conditions are inconsistent or unknown |
| `CUI-PRO-012` | ERROR | `ENCODE_ROUNDTRIP_FAILED` | Encoded payload does not self-decode to requested semantics |
| `CUI-PRO-013` | ERROR | `YAML_SCHEMA_INVALID` | Protocol YAML failed compiler/schema validation |
| `CUI-PRO-014` | ERROR | `GENERATED_ARTIFACT_DRIFT` | Checked-in/generated artifact differs from YAML output |
| `CUI-PRO-015` | WARN | `ROUTE_INVALID` | Message route/sender/receiver expectation was violated |
| `CUI-PRO-016` | WARN | `PLAUSIBILITY_FAILED` | Cross-signal or test-specific plausibility rule failed |

## 10. Sessions, ownership, and transmission

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-TX-001` | WARN | `BENCH_TX_DISABLED` | Mutating physical request was rejected because Bench TX is off |
| `CUI-TX-002` | WARN | `SOURCE_NOT_OWNED` | Session/job does not own the requested bus and CAN ID |
| `CUI-TX-003` | ERROR | `SOURCE_CONFLICT` | Physical or other producer conflicts with active source ownership |
| `CUI-TX-004` | WARN | `LEASE_EXPIRED` | Stimulus/source lease expired before or during work |
| `CUI-TX-005` | WARN | `MANIFEST_EXPIRED` | Resolved TX/test manifest passed its validity deadline |
| `CUI-TX-006` | ERROR | `MANIFEST_MISMATCH` | Requested operation differs from the enabled manifest |
| `CUI-TX-007` | WARN | `SCHEDULER_DEADLINE_MISSED` | Job instance missed its permitted execution deadline |
| `CUI-TX-008` | WARN | `PERIOD_MISSED` | Periodic instance was skipped rather than burst late |
| `CUI-TX-009` | WARN | `JITTER_EXCEEDED` | Submission jitter exceeded test/config tolerance |
| `CUI-TX-010` | ERROR | `JOB_CANCEL_FAILED` | Owned scheduled job did not terminate cleanly |
| `CUI-TX-011` | WARN | `COMMAND_INTENT_STALE` | Interactive intent expired or arrived out of order |
| `CUI-TX-012` | WARN | `PROFILE_DISALLOWS_TX` | Current profile does not permit the requested destination |
| `CUI-TX-013` | WARN | `RAW_TX_NOT_ALLOWED` | Raw/negative injection lacks explicit capability/policy |
| `CUI-TX-014` | ERROR | `AUTOMATIC_FIELD_FAILED` | Counter/checksum/forced field could not be generated |
| `CUI-TX-015` | INFO | `TX_SUBMITTED_DELIVERY_UNKNOWN` | Adapter-library submission succeeded; ECU delivery and acceptance remain unknown unless separately evidenced |
| `CUI-TX-016` | CRITICAL | `STOP_ALL_INCOMPLETE` | One or more owned TX jobs/leases survived Stop All |

## 11. ECU and message health

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-ECU-001` | WARN | `MESSAGE_LATE` | YAML-defined periodic message missed its live deadline |
| `CUI-ECU-002` | ERROR | `MESSAGE_MISSING` | Message exceeded its YAML-defined offline deadline |
| `CUI-ECU-003` | ERROR | `REQUIRED_ECU_EVIDENCE_MISSING` | Required ECU-defining messages are missing for the test/profile; this does not prove that the ECU is absent, unpowered, or faulty |
| `CUI-ECU-004` | ERROR | `HEARTBEAT_FROZEN` | Heartbeat frames arrive without alive-counter advancement |
| `CUI-ECU-005` | WARN | `ECU_DATA_INVALID` | ECU traffic is present but fails current validity requirements |
| `CUI-ECU-006` | INFO | `ECU_RECOVERING` | Valid advancing data resumed but stability window is incomplete |
| `CUI-ECU-007` | ERROR | `PHYSICAL_SYNTHETIC_CONFLICT` | Adapter-received traffic appeared for a synthetically owned ID; ordinary CAN traffic cannot prove the electrical sender identity |
| `CUI-ECU-008` | WARN | `UNEXPECTED_MESSAGE_PRESENT` | Message expected to be absent appeared during a test window |
| `CUI-ECU-009` | WARN | `DIAGNOSTIC_ACTIVE` | ECU-reported diagnostic/fault became active |
| `CUI-ECU-010` | INFO | `DIAGNOSTIC_CLEARED` | ECU-reported diagnostic/fault cleared |

For `CUI-ECU-009/010`, include `ecu`, `ecu_fault_code`, `fault_name`, `raw_value`, and YAML definition reference. Do not claim physical sender identity beyond the expected sender associated with the CAN ID.

## 12. Test runner and assertions

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-TST-001` | ERROR | `DEFINITION_INVALID` | Test definition failed schema/semantic validation |
| `CUI-TST-002` | WARN | `PRECONDITION_NOT_MET` | Test could not start because a required state/topology was absent |
| `CUI-TST-003` | ERROR | `ASSERTION_FAILED` | Valid contradictory evidence proved expected behavior false |
| `CUI-TST-004` | ERROR | `ASSERTION_TIMEOUT` | Required evidence did not arrive within the assertion window |
| `CUI-TST-005` | WARN | `ASSERTION_UNKNOWN` | Predicate result remained Unknown because required data was unavailable |
| `CUI-TST-006` | ERROR | `CLEANUP_FAILED` | Test cleanup did not complete as declared |
| `CUI-TST-007` | INFO | `TEST_CANCELED` | User/client/backend canceled the test with a recorded reason |
| `CUI-TST-008` | ERROR | `EVIDENCE_INCOMPLETE` | Relevant capture/timestamp/storage/assertion evidence has a gap |
| `CUI-TST-009` | ERROR | `INFRASTRUCTURE_LOSS` | Adapter/backend infrastructure failed during the test |
| `CUI-TST-010` | WARN | `TOPOLOGY_MISMATCH` | Present physical/synthetic roles differ from test requirements |
| `CUI-TST-011` | WARN | `PROTOCOL_NOT_COMPARABLE` | Protocol/topology prevents baseline comparison |
| `CUI-TST-012` | ERROR | `TEST_RUNNER_FAILED` | Runner service failed independently of ECU behavior |

`CUI-TST-003` normally produces `Fail`. Codes `004`, `005`, `008`, `009`, and infrastructure-caused `006` normally produce `Inconclusive`, according to the assertion/evidence dependency.

## 13. Recording, evidence, and replay

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-REC-001` | ERROR | `RECORDING_START_FAILED` | Recording could not be initialized before stimulus |
| `CUI-REC-002` | ERROR | `RECORDING_WRITE_FAILED` | Raw/event data could not be persisted |
| `CUI-REC-003` | ERROR | `RECORDING_QUEUE_OVERFLOW` | Lossless recording queue could not retain all evidence |
| `CUI-REC-004` | CRITICAL | `STORAGE_FULL` | Target storage has no safe remaining capacity |
| `CUI-REC-005` | ERROR | `FINALIZE_FAILED` | Recording metadata/index could not be finalized |
| `CUI-REC-006` | ERROR | `INTEGRITY_CHECK_FAILED` | Recording checksum/index/sequence verification failed |
| `CUI-REC-007` | WARN | `EXPORT_FAILED` | Requested evidence export could not be produced |
| `CUI-REC-008` | ERROR | `TRIGGER_BUFFER_LOSS` | Protected pre/post-trigger data was evicted or unavailable |
| `CUI-REP-001` | ERROR | `REPLAY_OPEN_FAILED` | Capture could not be opened or validated for replay |
| `CUI-REP-002` | ERROR | `REPLAY_PROTOCOL_MISMATCH` | Required protocol semantics are unavailable/incompatible |
| `CUI-REP-003` | WARN | `REPLAY_SEEK_FAILED` | Replay could not restore/advance to requested position |
| `CUI-REP-004` | ERROR | `REPLAY_CLOCK_FAILED` | Virtual replay time became inconsistent |
| `CUI-REP-005` | WARN | `REPLAY_INDEX_INCOMPLETE` | Seek/index data is incomplete; sequential replay may remain possible |

## 14. WebSocket and presentation

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-STR-001` | ERROR | `HANDSHAKE_FAILED` | Stream version/protocol/capability handshake failed |
| `CUI-STR-002` | WARN | `STREAM_SEQUENCE_GAP` | Client detected missing WebSocket batch sequence |
| `CUI-STR-003` | WARN | `CLIENT_QUEUE_OVERFLOW` | One client’s outbound queue dropped/coalesced beyond policy |
| `CUI-STR-004` | ERROR | `SNAPSHOT_FAILED` | Atomic initial/recovery snapshot could not be built/delivered |
| `CUI-STR-005` | WARN | `STREAM_STALLED` | WebSocket exists but heartbeat/batch progress stopped |
| `CUI-STR-006` | WARN | `SLOW_CLIENT` | Client consumption latency exceeded its budget |
| `CUI-STR-007` | ERROR | `STREAM_SCHEMA_UNSUPPORTED` | Event/batch schema version is incompatible |
| `CUI-UI-001` | WARN | `PRESENTATION_DELAYED` | Receive-to-visible age exceeded presentation budget |
| `CUI-UI-002` | WARN | `PRESENTATION_DROPPING` | Browser intentionally skipped/coalesced visual updates |
| `CUI-UI-003` | ERROR | `CLIENT_STATE_DESYNCHRONIZED` | UI/client local projection cannot be reconciled without snapshot |
| `CUI-UI-004` | ERROR | `RENDER_EXCEPTION` | React/rendering component raised an uncaught error |

Presentation-only errors do not make backend evidence incomplete unless the test explicitly requires visual evidence.

## 15. Vehicle projection

| Catalog ID | Default | Symbolic condition | Meaning |
|---|---:|---|---|
| `CUI-PRJ-001` | INFO | `SOURCE_MISSING` | Required actuation/sensor projection source is unavailable |
| `CUI-PRJ-002` | WARN | `SOURCE_STALE` | Projection source exceeded its YAML deadline |
| `CUI-PRJ-003` | ERROR | `EPOCH_MISMATCH` | Projection attempted to combine different adapter/replay epochs |
| `CUI-PRJ-004` | WARN | `INTEGRATION_DISCONTINUITY` | Center-locked projected pose required a new segment/reset |
| `CUI-PRJ-005` | WARN | `ACTUATION_SENSOR_DIVERGENCE` | Command and feedback differ beyond configured tolerance/window |
| `CUI-PRJ-006` | ERROR | `GEOMETRY_INVALID` | Dimensions/units/steering convention cannot produce valid geometry |

## 16. Logging behavior

### 16.0 How codes are produced

Codes are emitted by the backend service that owns the relevant fact. React, an LLM adapter, and the CLI do not derive codes from text or independently decide that a backend condition occurred.

| Detector boundary | Evidence used | Codes produced |
|---|---|---|
| FastAPI middleware and Pydantic handlers | request/schema/version/deadline/idempotency result | `CUI-API-*` |
| Capability/session service | granted capabilities, revision, expiry, current profile | `CUI-API-004/007`, `CUI-TX-001/004/012/013` |
| Adapter supervisor | worker lifecycle heartbeat, selected USB identity, receive exceptions, reconnect result | `CUI-ADP-*` |
| CAN adapter wrapper | per-channel open/configuration, supported controller evidence, submit result | `CUI-CAN-*` |
| Instrumented queues/router | sequence, capacity, depth, dropped count, epoch | `CUI-RX-*` |
| Generated protocol runtime | bus/ID/DLC/decode/encode/range/checksum/counter/route rules | `CUI-PRO-*` |
| Freshness/ECU topology service | YAML deadlines, alive-counter advancement, defining messages, provenance | `CUI-ECU-*` |
| Ownership/TX scheduler | Bench TX, owner/lease, deadlines, periods, jitter, cancel result | `CUI-TX-*` |
| Test runner | definition, preconditions, assertions, infrastructure dependencies, cleanup | `CUI-TST-*` |
| Recording/replay service | queue/write/storage/index/hash/virtual-clock outcomes | `CUI-REC-*`, `CUI-REP-*` |
| Subscription hub/client heartbeat | handshake, per-client sequence/queue/age | `CUI-STR-*` |
| React error boundary and render telemetry | render exception and measured visible age/drop | `CUI-UI-*` |
| Projection service | source dependency state, epoch, geometry and command/feedback tolerances | `CUI-PRJ-*` |
| Logging pipeline supervisor | queue capacity, writer/sink result, aggregation-key bounds | `CUI-LOG-*` |
| Top-level service supervisor | otherwise unhandled exception or invariant | `CUI-GEN-001/008` |

Domain services return typed outcomes/violations. A central event factory validates the code against the registry, supplies common correlation/timestamp/version fields, enforces bounded/redacted context, and appends the event. It does not reinterpret the owning service’s decision.

Do not map exception-message substrings throughout the codebase. Adapter-specific low-level exceptions are normalized once in the adapter wrapper using exception type and structured driver/USB status. Unknown exceptions become `CUI-GEN-001` with a redacted server-side stack trace.

### 16.0.1 State transitions, not polling guesses

Persistent conditions use explicit state machines:

```text
inactive → raised → updated → recovered
```

The detector emits only on a transition or bounded summary interval. For example, message freshness emits `CUI-ECU-001` when entering Late, `CUI-ECU-002` when entering Missing, and a recovered transition when valid advancing data stabilizes. The UI and LLM read these backend events; they do not apply their own timeout logic.

### 16.0.2 Causal chains

One root failure can produce legitimate consequences without pretending they are independent causes:

```text
CUI-ADP-007 DEVICE_REMOVED                 root
  ├→ CUI-TX-004 LEASE_EXPIRED              caused consequence
  ├→ CUI-TST-009 INFRASTRUCTURE_LOSS       test disposition cause
  └→ CUI-TST-008 EVIDENCE_INCOMPLETE       evidence consequence
```

Consequences reference `cause_event_id` and the shared `root_event_id`. Message/ECU freshness may transition after adapter loss, but the topology service marks it `transport_unknown` and suppresses misleading independent `REQUIRED_ECU_EVIDENCE_MISSING` claims until transport evidence is healthy again.

### 16.1 Destinations

- Structured JSON Lines operational log for application events.
- Human console rendering for local development.
- Immutable recording event stream for events relevant to a test/session.
- API/WebSocket event delivery using the same code and fields.
- Metrics derived from events/counters, not parsed from log text.

The authoritative recent/persisted event store is backend-owned and queryable through the shared API. Direct filesystem access to log files is not required for React, LLMs, tests, or CI.

Raw CAN recording remains separate from the operational log. Logs reference raw evidence by sequence/time rather than duplicating every high-rate frame.

### 16.2 Deduplication and recovery

Logging is not the high-rate evidence channel. Every detector evaluates every applicable frame and updates exact counters, but repeated observations of one active condition do not each become log lines.

#### 16.2.1 Per-condition episode state

The registry defines a bounded `aggregation_key` for each code, normally `code + session + adapter_epoch + bus + CAN ID/message + detector`. Dynamic values such as payload, exception text, expected/actual value, request ID, and signal value are never key fields. This prevents a changing bad value from manufacturing a new log stream every millisecond.

For each key, maintain one condition episode:

```text
healthy
  └─ first failure ─→ active(condition_instance_id, count=1)
                         ├─ repeat ─→ count++, update last/worst/sample
                         ├─ reminder deadline ─→ one aggregated update
                         └─ clear policy satisfied ─→ recovered(final count/duration)
```

The write policy is:

1. Emit the first `raised` record immediately.
2. Count every repeat exactly in memory/metrics and preserve relevant raw evidence; do not enqueue another identical log record.
3. Emit an `updated` summary after 1 second, then at most every 10 seconds while active by default. Each summary contains the interval count/rate, cumulative count, first/last time, and bounded representative evidence.
4. Emit `recovered` immediately when the code-specific clear policy is satisfied, with final count and duration.
5. A later recurrence starts a new `condition_instance_id`.

Thus a checksum failure observed every 1 ms for 10 seconds produces about three durable diagnostic records—raised, summaries, and recovery as applicable—not 10,000 lines, while its exact `occurrence_count=10000` remains queryable.

The 1-second/10-second defaults are operational logging policy, configurable per code in the machine-readable error registry. They are not CAN protocol timing. Critical state transitions may use shorter reminders; low-value warnings may use longer ones.

#### 16.2.2 Flapping and recovery hysteresis

Recovery must be based on detector evidence, not silence or a generic timer. The registry declares its clear policy:

- frame validation may require a YAML-defined number of consecutive valid frames;
- message freshness clears only after valid advancing traffic stabilizes;
- adapter removal clears only after successful reopen/configuration and a new adapter epoch;
- queue overload clears only after depth remains below its low-water mark;
- one-shot operation failures recover when a later operation succeeds or the owning job ends.

An alternating valid/invalid frame therefore remains one active episode rather than producing a raised/recovered pair every 2 ms. Per-frame validity and raw evidence remain available separately, so hysteresis does not falsify the data.

#### 16.2.3 Noisy-neighbor and overload protection

Rate limiting is applied per aggregation key, not as one global error limiter. Each severity has a reserved output budget, so a checksum storm cannot consume all capacity and hide adapter removal, storage failure, or an unrelated ECU fault. First raise, severity escalation, adapter epoch change, evidence-quality transition, and recovery bypass normal reminder suppression.

A global token-bucket limit exists only as a final sink-protection layer. When it activates:

- discard `DEBUG`, then repetitive `INFO`, before higher severities;
- never block the CAN RX/router/control path on console, disk, network, or WebSocket logging;
- increment exact `log_records_dropped_total{sink,severity}` and expose queue depth/high-water;
- update one reserved `logging.pipeline_overloaded` health condition without recursively flooding that same pipeline;
- include dropped/aggregated counts in the next successful summary and test evidence quality.

Logging uses a bounded non-blocking producer queue and a dedicated writer/listener. Raw CAN recording has its own bounded queue and health state; operational logs and UI delivery cannot consume its capacity.

#### 16.2.4 Metrics, logs, and raw evidence have different jobs

| Signal | Purpose | High-rate behavior |
|---|---|---|
| Counter/metric | Exact frequency, rate, ratios, capacity, and alert thresholds | Increment every occurrence using bounded low-cardinality labels |
| Condition/event log | Explain first failure, important changes, causal chain, and recovery | State transitions plus periodic aggregate summaries |
| Raw CAN recording | Reproduce frame-level timing and payload evidence | Lossless when recording is enabled, or explicitly marked incomplete |
| Debug trace | Temporary deep investigation | Opt-in, time/size bounded, never the default bench mode |

Required low-cardinality metrics include `error_occurrences_total{code,domain,severity,bus}`, `error_records_emitted_total`, `error_occurrences_aggregated_total`, `active_conditions`, log queue depth/high-water, and dropped records by sink/severity. CAN ID, message name, session, request, event, raw value, and exception text are query fields in events/evidence, not unrestricted metric labels.

ECU-reported periodic flags follow the same rule. A fault bit repeated in MTR `0x206` at 50 Hz is one active reported condition with observation count/rate, not 50 new error events per second. Non-fault status bits such as `STARTUP_READY`, mode, ESTOP active, or brake engaged remain state transitions and are not automatically assigned error severity. TEC/REC samples and wrapping/saturating firmware counters are gauges/reported counters with declared semantics, not log messages.

Backend operational events and ECU-reported conditions remain separate namespaces and evidence bases. Existing RT/SYS `ESP_LOG*` console text is not visible over CAN and must not be invented from related frames. If a future serial collector ingests firmware events, it records `source_transport=uart`, firmware identity/version, and the original structured firmware code; plain English matching is prohibited.

#### 16.2.5 Cardinality guard

The registry specifies allowed aggregation fields and a maximum number of active keys per detector. If malformed or unknown traffic exceeds that bound, excess values collapse into a visible overflow group with total count and bounded representative samples. The backend emits `logging.cardinality_limit_reached` once for the episode. It does not allocate unbounded state or silently discard the fact that distinct conditions were collapsed.

The UI displays active condition rows—not repeated lines—with ID, readable code, latest message, count, rate, first seen, last seen, severity, and state. A separate event timeline shows the raised/summary/recovered records. LLM/API queries return the same aggregation fields.

This policy follows the common operational split between structured events, metrics, and rate-limited hot-path logging: OpenTelemetry defines structured log/event fields and `error.type`; Prometheus recommends a counter corresponding to failures/log sites; Linux recommends rate-limited or one-time logging in hot paths; syslog defines interoperable severity levels.

### 16.3 Exception handling

Expected domain failures return their specific symbolic codes without Python stack traces in normal API responses. Unexpected exceptions emit `system.unhandled_exception` (`catalog_id: CUI-GEN-001`) with a server-side stack trace, request/session context, and redacted response error ID. The stack trace is not exposed to ordinary clients.

### 16.4 Retention

- Rotate operational logs by bounded size/time.
- Retain logs referenced by a test report with that report’s evidence policy.
- Store firmware/software/protocol versions with recordings.
- Redact tokens and environment secrets at the logging boundary.
- Make storage failure visible through `CUI-REC-*`; never silently stop logging evidence.

### 16.5 Logging standards references

- [OpenTelemetry Logs Data Model](https://opentelemetry.io/docs/specs/otel/logs/data-model/)
- [OpenTelemetry semantic conventions for events](https://opentelemetry.io/docs/specs/semconv/general/events/)
- [RFC 5424 syslog severity and structured data](https://datatracker.ietf.org/doc/html/rfc5424)
- [Prometheus instrumentation practices](https://prometheus.io/docs/practices/instrumentation/)
- [Linux kernel hot-path logging guidance](https://docs.kernel.org/core-api/printk-basics.html#avoiding-lockups-from-excessive-printk-use)
- [systemd-journald rate limiting and storage bounds](https://www.freedesktop.org/software/systemd/man/journald.conf.html)

## 17. API error example

```json
{
  "type": "https://etrike.local/problems/tx/source-conflict",
  "title": "CAN source conflict",
  "status": 409,
  "detail": "Low-bus VCU_SES_REQ is already produced by a physical source.",
  "instance": "/api/v1/events/evt_456",
  "code": "tx.source_conflict",
  "catalog_id": "CUI-TX-003",
  "request_id": "req_123",
  "retryable": false,
  "context": {
    "bus": "low",
    "can_id": "0x169",
    "message": "VCU_SES_REQ",
    "current_owner": "physical_expected_source"
  },
  "evidence_refs": ["evt_456"]
}
```

## 18. Initial implementation priority

Implement these groups first because they protect result correctness:

1. `CUI-GEN-*`, `CUI-LOG-*`, `CUI-API-*`, and structured fields.
2. `CUI-ADP-*`, `CUI-CAN-*`, and `CUI-RX-*` for connection/loss evidence.
3. `CUI-PRO-*` for corruption and protocol validation.
4. `CUI-TX-*` for scheduler, ownership, and Stop All.
5. `CUI-TST-*` and `CUI-REC-*` for trustworthy verdicts/evidence.
6. `CUI-STR-*`, `CUI-UI-*`, and `CUI-PRJ-*` for client/presentation diagnosis.

## 19. Machine-readable registry and API access

Maintain one registry containing mandatory symbolic `code`, mandatory `catalog_id`, domain, default severity, retryability, description, message template, allowed context schema, problem-type URI, HTTP status where applicable, and documentation link. Generate or validate from it:

- Python enum/Pydantic event models;
- OpenAPI error/event schemas;
- TypeScript constants/types for React;
- LLM tool result schemas;
- documentation tables and contract fixtures.

The shared backend API exposes:

```text
GET  /api/v1/error-codes
GET  /api/v1/events
GET  /api/v1/events/{event_id}
POST /api/v1/events/query
POST /api/v1/events/wait
GET  /api/v1/events/summary
GET  /api/v1/events/export
WS   /api/v1/stream  subscription: events
```

Filters include time/sequence range, code/domain/severity/state, request/session/job/test, adapter epoch, bus, CAN ID/message/signal, ECU, source/provenance, root cause, and whether the event affected evidence.

`events/summary` is deterministic backend aggregation: counts, first/last occurrence, active/recovered state, duration, affected sessions/tests, root causes, and evidence links. It does not ask an LLM to infer counts from raw text.

LLM tools are thin mappings over these same endpoints:

```text
list_error_codes
query_events
get_event
wait_for_event
summarize_session_events
export_event_evidence
```

React uses the same resources for diagnostics and timelines. Access is capability-based (`observe_events`, `export_events`, optional `view_internal_diagnostics`), never granted or denied merely because the client is an LLM.

API event results exclude secrets and unrestricted stack traces. The `view_internal_diagnostics` capability may reveal bounded server diagnostic fields locally, while raw secrets remain redacted unconditionally.
