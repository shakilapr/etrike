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
  ├→ generated/translated LLM tool schemas
  └→ optional thin CLI client
```

OpenAPI is the machine-readable discovery contract. Do not maintain a second command-schema format for LLMs or the CLI. API and generated-client compatibility is checked in CI.

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
/api/v1/stream
```

Resource names describe the domain rather than a particular client screen. React may compose several resources into one workspace. An LLM may call the same resources as tools.

## 4. Response and error model

Every REST response uses the same versioned envelope:

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

Accepted mutations additionally return session ID/revision and, when asynchronous, a job ID. Errors have stable codes and structured details; no client must parse English strings.

Mutations accept a request ID and idempotency key where retry could duplicate work. Commands that modify a session accept an expected revision when concurrent changes matter.

## 5. Live stream

Clients connect to the same versioned WebSocket and request subscriptions:

- critical transport/test/integrity events;
- coalesced latest state;
- test/job progress;
- vehicle projection;
- raw CAN batches only when explicitly requested.

Every batch carries adapter/replay epoch and sequence boundaries. Each client has an independent bounded queue. A gap makes that client view degraded and causes a fresh atomic snapshot. UI rendering loss or an LLM disconnect cannot delay CAN capture, tests, scheduler work, or recording.

An LLM normally uses snapshot, query, wait, and test endpoints instead of consuming every CAN frame. The option to subscribe to raw batches remains the same for all authorized clients.

## 6. LLM integration

The LLM receives typed tools generated or wrapped from OpenAPI. A small MCP/native-tool adapter may improve tool names and descriptions, but it only translates calls:

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
```

These are not privileged alternatives to the UI. React can issue the equivalent requests and receives identical results.

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
2. Generated React and LLM schemas come from the same OpenAPI version.
3. No domain service checks the client type to choose behavior.
4. A client disconnect does not orphan scheduled traffic.
5. A slow live-stream client cannot affect other clients or backend real-time work.
6. Pure Software tests run without the React UI.
7. Full authorized API access still cannot bypass application invariants by reaching internal implementation objects.
