# Payload Strategies and Integrity Profiles

In the E-Trike architecture, the CAN protocol layer acts as a strict boundary. We use static YAML dictionaries to define the wire format, but simply generating structs isn't enough when dealing with safety-critical or legacy third-party vendor hardware.

To handle this, every message in the system must declare exactly one **Payload Strategy**. This tells the protocol compiler *how* the bytes should be transformed into application data.

---

## 1. The `generated` Strategy

This is the standard approach for messages owned entirely by the project (e.g., internal ECU-to-ECU telemetry).

- **How it works:** The YAML compiler reads the bit-level layout (start bit, length, signedness, scale) and automatically generates deterministic C++ (for firmware) and Python (for host/testing) codecs.
- **When to use:** For standard telemetry, simple commands, and internal state reports where a basic stateless decoding is sufficient.

## 2. The `profile` Strategy

Many automotive protocols require end-to-end (E2E) protection, such as rolling counters and checksums, to guarantee that a message hasn't been corrupted or delayed. 

Instead of writing a custom decoder for every message, we use **Named Profiles**.

- **How it works:** A named profile is a small, reusable, versioned integrity implementation (e.g., `vendor_xor8_v1` or an AUTOSAR E2E profile). The YAML definition simply tags the message with the profile ID. The system generates the basic data layout, but execution is passed through the chosen profile to validate the checksum.
- **When to use:** When multiple messages share the same standard integrity algorithm.

## 3. The `custom` Strategy

When integrating with third-party hardware (like an off-the-shelf electronic steering module or ABS unit), the hardware often uses proprietary, complex, or undocumented data packing that cannot be easily expressed in a static YAML file.

- **How it works:** The message is tagged as `custom`. The system does NOT generate an ordinary codec. Instead, developers must provide an explicit, handwritten codec implementation.
- **The rule:** Custom codecs must be accompanied by a manual mapping record and a reviewed "wire hash." This guarantees that while the code is handwritten, it is strictly tracked and cannot change without review.

---

## Why this matters

By forcing every message to declare its strategy, the codebase avoids "hidden" parsers. A developer looking at a message definition instantly knows whether the decoder was auto-generated, uses a standard safety profile, or is a hand-written vendor exception. This makes auditing and testing significantly easier.
