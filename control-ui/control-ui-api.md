# Control UI Shared API and Client Contract

**Purpose:** Define one backend contract that serves the React UI, LLM tool clients, engineers, CI, and an optional thin terminal client without duplicating behavior.

## 1. Core rule

There is no separate UI backend, LLM backend, terminal backend, or automation API.

```text
React UI ─────────┐
LLM tool adapter ─┼→ FastAPI REST/WebSocket API → application services → CAN
Thin CLI / CI ────┘
```

All clients use the same:

- Pydantic request and response models;
- session and capability rules;
- YAML-generated protocol catalog/codecs;
- adapter manager and connection state;
- source ownership and Bench TX state;
- scheduler, counters, checksums, and leases;
- predicates, test runner, and verdicts;
- recording, replay, projection, audit, and evidence services.

The backend never branches domain behavior based on `ui`, `llm`, or `cli`. Caller identity is recorded for audit, while authorization depends only on granted capabilities, session profile/state, adapter epoch, protocol hash, source ownership, and the requested operation.

## 2. Shared contract generation

Pydantic models define the API once:

```text
Pydantic models
  ├→ FastAPI validation
  ├→ OpenAPI document
  ├→ generated TypeScript client for React
  ├→ source schemas for an optional LLM tool adapter
  └→ optional thin CLI client
```

OpenAPI is the machine-readable description of the normal API. It does not execute requests and does not automatically give an LLM network access. Do not maintain a second domain contract for LLMs or the CLI; when a client platform requires tool schemas, derive the thin translation from OpenAPI and test it against the same API. API and generated-client compatibility is checked in CI.

### 2.1 What OpenAPI compatibility means

FastAPI serves the working REST endpoints and also publishes their description, normally at `/openapi.json` (this project may version it as `/api/v1/openapi.json`). The description contains paths, methods, parameters, JSON schemas, responses, errors, and authentication requirements.

```text
FastAPI routes       = operations clients actually call
OpenAPI JSON         = machine-readable description of those operations
Swagger/ReDoc        = human interfaces generated from that description
```

There is no behavior difference between a request from React, Python, or Claude integration software. The only difference is which client transports the same HTTP request.

Every operation must provide OpenAPI with:

- stable unique `operationId`;
- concise summary and precise behavioral description;
- side effects and whether the operation creates a job;
- required capability and valid session/profile states;
- complete request/response/error schemas;
- units, enums, bounds, defaults, and examples;
- idempotency and expected-revision requirements;
- timeout/deadline behavior;
- evidence and cleanup behavior;
- all relevant HTTP status responses.

Descriptions must state that `accepted`, `scheduled`, `submitted`, and `observed/accepted by ECU` are different dispositions.

## 3. API responsibilities

REST handles atomic snapshots, queries, deliberate commands, and job lifecycle. WebSocket carries subscribed live deltas and events. High-rate work remains in the backend.

Initial resource groups:

```text
/api/v1/capabilities
/api/v1/status
/api/v1/protocol
/api/v1/adapters
/api/v1/sessions
/api/v1/state
/api/v1/query
/api/v1/wait
/api/v1/injections
/api/v1/synthetic-peers
/api/v1/tests
/api/v1/recordings
/api/v1/replays
/api/v1/projection
/api/v1/evidence
/api/v1/error-codes
/api/v1/events
/api/v1/stream
```

Resource names describe the domain rather than a particular client screen. React may compose several resources into one workspace. An LLM may call the same resources as tools.

## 4. Response and error model

Successful REST responses use the versioned envelope:

```json
{
  "schema_version": 1,
  "request_id": "req_123",
  "ok": true,
  "data": {},
  "warnings": [],
  "errors": [],
  "evidence": []
}
```

Accepted mutations additionally return session ID/revision and, when asynchronous, a job ID.

HTTP request failures use RFC 9457 `application/problem+json`, the appropriate HTTP status, and extensions from the shared event contract. Every error includes a fixed catalog ID such as `CUI-ADP-007`, a readable symbolic code such as `adapter.device_removed`, a contextual message/detail, and a unique event ID. Clients can display the ID and message, filter reliably by either stable identifier, and never need to parse English strings.

Test verdicts and other expected domain outcomes are resource data, not HTTP failures. A successfully executed test that returns `Fail` or `Inconclusive` normally returns `200`; an accepted asynchronous test returns `202` and a job resource. A bounded wait that simply reaches its requested duration returns `200` with `disposition: timeout`.

Mutations accept a request ID and idempotency key where retry could duplicate work. Commands that modify a session accept an expected revision when concurrent changes matter.

All errors and significant recoveries use the shared catalog in `error-codes.md`. The same symbolic `code`/OpenTelemetry `error.type` and structured context appear in operational logs, API responses, WebSocket events, recordings, React, LLM access, and test evidence. HTTP failures additionally expose a stable problem `type` URI. Native driver, SocketCAN, UDS, or J1939 identifiers are preserved only when those layers actually report them.

### 4.1 Minimum data clients need

The API must expose enough structured data for a client to reason without reading source code or parsing display text.

**Capabilities and compatibility:**

```text
api_version
backend_version / process_instance_id
protocol_semantic_hash / exact_source_hash
error_registry_version
supported profiles and operations
granted capabilities
adapter-supported/unknown metrics
stream schema version
```

**Backend and adapter status:**

```text
readiness and startup blockers
USB presence and selected adapter identity
adapter worker state, epoch and last error
High/Low channel configured state and bitrate
channel Active/Quiet state and last RX age
per-channel RX/TX/error/loss counters where supported
queue depth, high-water, dropped count and oldest age
storage/recording health
active session, Bench TX, leases and jobs
```

**Atomic CAN/state snapshot:**

```text
snapshot sequence and mapped timestamp
adapter/replay epoch
bus, CAN ID, message and expected source
raw payload and DLC
decoded engineering signals with units
latest observation and latest valid value separately
sample/arrival timestamps and age/deadline
validity and failed rules
counter/checksum state
count, observed period/rate and changed-byte mask
physical/synthetic/replay/requested provenance
ECU liveness/topology state
actuation and sensor vehicle projections
```

**Mutation and asynchronous job result:**

```text
request/session IDs and new session revision
disposition: rejected/accepted/queued/submitted/canceled/failed
job ID, owner, adapter epoch and expiry
resolved semantic values and TX manifest hash
bus/ID/rate/count/duration and automatic fields
requested deadline and actual submission/jitter metrics
progress, test step and cleanup state
Pass/Fail/Inconclusive verdict and evidence quality
error events and evidence references
```

Large raw histories are returned by bounded query/export resources, not embedded in ordinary snapshots.

## 5. Live stream

Clients connect to the same versioned WebSocket and request subscriptions:

- critical transport/test/integrity events;
- coalesced latest state;
- test/job progress;
- vehicle projection;
- raw CAN batches only when explicitly requested.

Every batch carries adapter/replay epoch and sequence boundaries. Each client has an independent bounded queue. A gap makes that client view degraded and causes a fresh atomic snapshot. UI rendering loss or an LLM disconnect cannot delay CAN capture, tests, scheduler work, or recording.

An LLM normally uses snapshot, query, wait, and test endpoints instead of consuming every CAN frame. The option to subscribe to raw batches remains the same for all authorized clients.

Backend operational/error events are also first-class shared resources. React, LLMs, Python tests, and CI can query, wait for, summarize, export, or subscribe to the same structured events. They do not read server console text or duplicate error-detection logic. Event access and redaction follow capabilities, not client type; see `error-codes.md`.

## 6. LLM integration

OpenAPI alone does not make Claude call the backend. The Claude host must have a way to execute HTTP requests. Supported integration choices are:

| Claude environment | Simplest connection |
|---|---|
| Claude Code | Run the shared Python HTTP client or `curl` through its permitted terminal tools |
| Application using Anthropic Messages API | Application defines client-side tool schemas, executes the corresponding FastAPI request, and returns the result to Claude |
| Claude Desktop/Claude.ai or another MCP client | Optional thin MCP server translates MCP operations to FastAPI requests |
| Custom agent runtime with OpenAPI import | Runtime imports selected OpenAPI operations and performs HTTP calls |

Claude itself does not directly execute arbitrary network calls in the Messages API; the hosting application executes requested client tools. Claude Code may call the local API through terminal commands when permissions allow. Claude products also support MCP as an external-tool integration mechanism.

When an LLM tool adapter is needed, its schemas are generated or translated from selected OpenAPI operations. It may improve operation names and descriptions, but it only translates calls:

```text
LLM tool call → API request → shared backend service → API result
```

The adapter contains no CAN transport, codec, scheduler, liveness, test, or permission logic.

Useful task-level tools map directly to shared API operations:

```text
get_capabilities
get_status
get_state_snapshot
query_can_state
wait_for_condition
preview_injection
apply_injection
run_test
get_test_result
stop_all
get_evidence
list_error_codes
query_events
wait_for_event
summarize_session_events
```

These are not privileged alternatives to the UI. React can issue the equivalent requests and receives identical results.

### 6.1 Direct HTTP requirements

A direct client needs only:

```text
base URL, normally loopback
API version
session capability token
client/request identity
JSON request body
bounded timeout
```

For local direct access:

```http
Authorization: Bearer <session-capability>
X-Request-ID: req_123
X-Client-Instance: claude-code-run-123
Idempotency-Key: idem_456
```

`X-Client-Instance` is audit/correlation data and never changes domain behavior. Tokens are not placed in prompts, logs, URLs, or error context.

### 6.2 LLM-friendly operations without WebSocket

Some Claude hosts can call HTTP but cannot maintain WebSockets. Therefore every important diagnostic/test workflow must be possible using bounded REST operations:

```text
atomic state snapshot
structured query
wait for typed condition with deadline
start job/test
poll or long-wait for job disposition
query/wait/summarize error events
fetch bounded evidence window
Stop All
```

WebSocket remains the efficient live path for React and capable clients; it is not mandatory for an LLM to test the backend correctly.

## 7. Optional thin terminal client

A large independent CLI is unnecessary. If terminal convenience is required, provide a small client over HTTP/WebSocket:

```bash
control-ui status --json
control-ui watch --message RT_STATE_RPT --ndjson
control-ui test run rt-startup --wait --json
control-ui inject preview --message HOST_DRIVE_CMD --set speed_mmps=500 --json
control-ui stop-all --session ses_123 --json
```

It may use Typer and HTTPX, but it contains no domain behavior. JSON is the normal automation output; NDJSON is used for terminal streaming. OpenAPI remains its discovery/source contract.

## 8. Capabilities and full access

Full access means access to every supported application API operation, not direct access to Python objects, USB handles, queues, arbitrary SQL, shell execution, or internal service methods.

Capabilities may include:

```text
observe
record
virtual_tx
physical_tx
raw_negative_test
adapter_admin
```

A trusted local LLM session may be granted the same full capability set as React. The backend still validates every operation. Capabilities do not bypass YAML bounds, adapter epochs, source conflicts, test ownership, evidence rules, or cleanup.

## 9. Physical bench behavior

Pure Software is the default unattended test profile. For a physical bench session:

1. Select and verify the adapter/channel mapping.
2. Create the session with `physical_tx` capability.
3. Explicitly enable Bench TX for a finite TTL.
4. Preview or plan traffic through the same endpoint used by React.
5. Apply the validated request or test manifest.
6. Let backend jobs own timing and cleanup.
7. Disable TX on Stop All, expiry, disconnect/reconnect, protocol mismatch, or session close.

The LLM does not have to remain connected for periodic timing, assertions, or cleanup. API acceptance means accepted by the backend; adapter submission and ECU response remain separately reported evidence.

## 10. UI and automation testing

The API supports deterministic virtual fixtures and a controllable test clock where required. Standard project commands run backend tests and Playwright directly; they do not need to be hidden behind a large Control UI CLI.

Headless tests:

- start the same FastAPI application in virtual mode;
- load fixture state through supported test setup APIs;
- wait on readiness rather than fixed sleeps;
- exercise React through accessible roles;
- inspect API/WebSocket results using the shared schemas;
- collect screenshots, traces, browser console errors, failed requests, and backend exceptions;
- run Stop All and verify no jobs/processes remain.

## 11. Acceptance criteria

The shared-client design is correct when:

1. The same operation made by React and an LLM produces the same validated request, state transition, job, evidence, and result.
2. The generated React client and any optional LLM/MCP schemas reference the same OpenAPI version and pass parity fixtures.
3. No domain service checks the client type to choose behavior.
4. A client disconnect does not orphan scheduled traffic.
5. A slow live-stream client cannot affect other clients or backend real-time work.
6. Pure Software tests run without the React UI.
7. Full authorized API access still cannot bypass application invariants by reaching internal implementation objects.
8. Claude Code can complete a virtual test using the shared Python/HTTP client without MCP.
9. An optional MCP/client-tool call produces the same backend request/result as direct HTTP.

## 12. Integration references

- [FastAPI OpenAPI metadata and schema URL](https://fastapi.tiangolo.com/tutorial/metadata/)
- [FastAPI automatic OpenAPI and client-generation features](https://fastapi.tiangolo.com/features/)
- [Anthropic Model Context Protocol overview](https://docs.anthropic.com/en/docs/mcp)
- [Anthropic Claude Code CLI and MCP entry point](https://docs.anthropic.com/en/docs/claude-code/cli-usage)
