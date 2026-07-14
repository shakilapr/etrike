# Contract-Based Protocol Ownership

When building complex multi-ECU systems with CAN buses, managing how messages are defined, sent, and received can quickly become a bottleneck. Early in a project's lifecycle, it is common to use **registration-based tracking**, where developers manually map bits and bytes from wire payloads into application structures. 

As the project scales, this approach leads to duplicated definitions, out-of-sync ECUs, and bugs caused by manually written parsers (e.g., overlapping layouts or endianness errors). 

To solve this, the E-Trike architecture uses a **Contract-Based Protocol Ownership** model. This transitions the project from a "Files & Mappings" approach to a "Contracts & Implementations" approach.

---

## 1. Decoupling Definitions from Logic

The core idea of contract-based design is that the **static wire dictionary (the "what")** is strictly separated from the **runtime policy and codec logic (the "how")**. 

In this model, the shared protocol repository is divided into distinct domains:

1. **Core:** Fixed frame types, bus enums, and codec status codes. Contains no application logic.
2. **Contracts (YAML):** The single source of truth for message layouts, signals, and network topology.
3. **Generated Codecs:** Deterministic C++/Python parsers and serializers generated directly from the contracts. These are strictly read-only and never edited manually.
4. **Manual/Adapter:** The *exclusive* home for complex vendor algorithms or exceptional behaviors that cannot safely be auto-generated (e.g., proprietary checksums).

---

## 2. The Single Source of Truth

Each message layout is defined **exactly once** in a static configuration file (like YAML). 

- **Owner-centric definitions:** Messages are grouped by the ECU that originates them (e.g., `rt.yaml`, `sys.yaml`, `host.yaml`), rather than the bus they happen to travel on.
- **No redefinition:** A receiver (like the SYS ECU) never maintains a second copy of a message defined by the sender (like the RT ECU). They both consume the same generated artifact.
- **Physical instances:** The runtime identity of a message is its `bus + CAN ID`. Its contract identity is `owner + message + bus`. A single layout definition can be reused across multiple physical buses without merging their state.

### Example

```yaml
name: RT_HEARTBEAT
sender: RT
instances:
  - {bus: high, id: 0x7FD, receivers: [Host], state_scope: independent}
  - {bus: low,  id: 0x7FD, receivers: [SYS],  state_scope: independent}
```

Here, `RT_HEARTBEAT` is defined once but exists on two separate buses with independent state.

---

## 3. Implementation Strategies

Instead of relying purely on auto-generation or purely on manual code, every message in the contract must declare a specific **Payload Strategy**:

1. **`generated`:** The standard approach. A stateless codec is automatically generated from the static layout.
2. **`profile`:** Uses a reusable, named, and versioned integrity implementation (e.g., a standard E2E profile or a repeated XOR checksum). 
3. **`custom`:** Used for legacy or third-party vendor hardware that requires a handwritten codec (e.g., a proprietary steering module protocol).

By forcing every message to declare its strategy, the system can automatically generate metadata and enforce conformance. If a message is `custom`, a manual mapping record tracks its stable ID, source message, wire hash, and affected build targets. 

This guarantees traceability without pretending that complex, stateful vendor logic can be naively auto-generated.

---

## 4. Policy Stays Local

While the codecs and definitions are shared, **runtime policy remains strictly local to each component**. 

The generated protocol layers do not know about:
- Allowed missed messages or timeout limits
- Escalation policies (e.g., triggering an ESTOP if a message is lost)
- Hardware pin configurations
- RTOS tasks or logging severity

This separation of concerns ensures that the protocol layer remains a pure, testable data transformation pipeline, while safety-critical decisions remain in the application layer of each ECU.
