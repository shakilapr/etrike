# CAN Bit Numbering and Endianness

This document clarifies the bit numbering conventions used in the E-Trike Debug Tool's YAML definitions and generated CAN artifacts.

The debug tool supports two primary bit numbering schemes: **Motorola** (Big Endian) and **Intel** (Little Endian). The schema uses an 8-byte payload representing a single 64-bit word.

## 1. Byte and Bit Offsets

In our YAML definitions, every signal specifies:
- `byte`: The starting byte index (0 through 7).
- `bit_offset`: The bit offset within that starting byte (0 through 7).
- `size`: The length of the signal in bits.

How these are mapped to the physical payload depends on the `byte_order` (Motorola vs. Intel).

## 2. Motorola (Big Endian)

When a signal uses `motorola` byte order:
- The Most Significant Byte (MSB) of the signal is located at the specified `byte`.
- The Most Significant Bit (MSB) of the signal starts at `bit_offset` within the starting `byte`.
- The signal "grows" towards higher byte indices (e.g. from Byte 0 into Byte 1) as you move from MSB to LSB.
- **Note:** In standard CAN documentation, Motorola signals are often described using a "start bit" which corresponds to the LSB, but our YAML generator explicitly anchors signals at the MSB byte and bit offset.

*Example:* A 16-bit Motorola signal starting at `byte: 0, bit_offset: 0` will occupy Byte 0 and Byte 1. Byte 0 contains the highest 8 bits; Byte 1 contains the lowest 8 bits.

## 3. Intel (Little Endian)

When a signal uses `intel` byte order:
- The Least Significant Byte (LSB) of the signal is located at the specified `byte`.
- The Least Significant Bit (LSB) of the signal starts at `bit_offset` within the starting `byte`.
- The signal "grows" towards higher byte indices (e.g. from Byte 0 into Byte 1) as you move from LSB to MSB.

*Example:* A 16-bit Intel signal starting at `byte: 0, bit_offset: 0` will occupy Byte 0 and Byte 1. Byte 0 contains the lowest 8 bits; Byte 1 contains the highest 8 bits.

## 4. Decoder Internal Representation

The `DynamicCanDecoder` and generated codec parse the 8-byte payload into a 64-bit `BigInt`. 
- `view.getBigUint64(0, false)` loads the 8 bytes as a Motorola (Big Endian) 64-bit integer.
- `view.getBigUint64(0, true)` loads the 8 bytes as an Intel (Little Endian) 64-bit integer.

Bit extraction then uses shifts (`>>`) and masks (`&`) across the entire 64-bit integer instead of iterating over individual bytes.
