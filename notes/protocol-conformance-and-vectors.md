# Protocol Conformance and Golden Vectors

In a multi-node, multi-language vehicle system, proving that different computers interpret the same network message correctly is notoriously difficult. For instance, the RT ECU (running C++) and the Jetson Orin (running Python via ROS 2) must agree exactly on the decoding of a Host Drive Command.

If the C++ encoder and the Python decoder have subtle differences (like endianness swapping or scaling rounding errors), the vehicle will behave unpredictably.

To solve this, the E-Trike architecture uses **Golden Vectors** for protocol conformance.

---

## 1. What is a Golden Vector?

A "Golden Vector" is a language-neutral test case that pairs a raw hexadecimal CAN payload (the input) with the exact, expected decoded values (the output).

For example:
```json
{
  "message": "HOST_DRIVE_CMD",
  "raw_payload": "0A 14 00 00 00 00 00 00",
  "expected": {
    "speed_mmps": 5130,
    "yaw_rate_mrad_s": 0,
    "gear": 1
  }
}
```

## 2. Cross-Language Conformance

These vectors are treated as absolute truth. During the Continuous Integration (CI) process, the exact same vector file is fed into:
1. **The C++ Unit Tests:** To prove the firmware codecs correctly encode/decode.
2. **The Python Unit Tests:** To prove the Host ROS 2 nodes and diagnostic tools correctly encode/decode.

If a developer changes the `host.yaml` contract and regenerates the codecs, they must also run the shared vectors against both sides. 

## 3. Protecting Exceptional Codecs

While generated codecs are inherently deterministic, **Custom Codecs** (handwritten parsers for third-party hardware) carry a high risk of developer error. 

By requiring a set of golden vectors for every custom message, we treat the handwritten codec as a "black box." The vectors prove that the custom C++ and custom Python code behave identically for all edge cases, without needing to statically analyze the algorithms themselves.

## The Rule of Conformance

The complete proof that a complex message is handled correctly is:
`Static Wire Definition + Codec Implementation + Language-Neutral Vectors`

A test passing in C++ alone is not enough; the algorithm is only considered correct if the golden vector passes in every supported language in the system.
