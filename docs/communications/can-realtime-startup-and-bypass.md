# CAN Real-Time Startup, Freshness, and Developer Bypass

This document defines the runtime contract for RT, SYS, and the low CAN bus.
It separates transport availability, actuator authority, and emergency-stop
handling so that a missing bench unit cannot be mistaken for a real ESTOP.

## 1. Startup and ACK ownership

RT enables its TWAI controller for reception at boot, but operational
transmission starts closed. A valid frame from a known low-bus unit proves that
an ACK-capable peer is present and opens TX admission. SYS is the normal
bootstrap peer: its periodic heartbeat or mode traffic is received and ACKed by
RT before RT begins its own periodic traffic.

If no peer is present, RT remains receive-capable and silent. It reports a
waiting/degraded condition and does not repeatedly transmit itself into
Error-Passive or Bus-Off.

CAN ACK is only link-level evidence that at least one other controller received
the frame. It is not evidence that the intended actuator accepted or executed a
command. Actuator authority therefore continues to depend on the intended
unit's heartbeat, status, operating mode, and feedback.

TX admission closes after 1500 ms without a valid low-bus frame. A later valid
frame reopens it only while the controller is not Bus-Off.

## 2. Real-time transmission policy

Periodic actuator messages are state, not history:

| Message | Period | Sender policy | Receiver deadline |
|---|---:|---|---:|
| `RT_DRIVE_CMD` | 10 ms | At most one pending/in-flight frame | 50 ms |
| `RT_BRAKE_CMD` | 20 ms | At most one pending/in-flight frame | 100 ms |
| `VCU_SES_REQ` | 20 ms | At most one pending/in-flight frame | Vendor supervision |
| `VCU_SEB_REQ` | 20 ms | At most one pending/in-flight frame | Vendor supervision |

All TWAI submissions are non-blocking. If the same actuator message is already
pending, the current cycle is skipped; the next control cycle regenerates the
latest value. Historical actuator values never build up behind one another.

Sparse safety events and diagnostics may use small bounded queues. ESTOP uses
priority delivery and local latching. Queue capacity is not treated as a
substitute for command freshness.

MTR requires three freshly received drive commands before it regards the stream
as usable. A deadline violation clears that qualification, commands zero speed
and neutral, and requires three fresh frames again.

SYS treats stale RT drive traffic as loss of RT authority. If an RT brake
setpoint exceeds its deadline while Auto is active, SYS uses the configured
fail-safe maximum brake policy rather than preserving the old pressure.

## 3. Developer bypass

Developer bypass changes dependency requirements; it does not disable safety.

Bypassed conditions:

- Never-seen or stale SYS, Host, MTR, SES, or SEB communication required only
  by absent development hardware.
- Missing actuator synchronization or following feedback for a unit explicitly
  bypassed.
- CAN unavailability caused by an intentionally absent ACK partner.

Always active:

- Physical ESTOP input.
- A valid received `SAFETY_ESTOP`.
- Explicit ESTOP mode.
- Local task/watchdog failures.
- Valid fault reports from hardware that is present.
- Command bounds and safe default outputs.

An absent bypassed unit receives no commands. Bypass does not create actuator
authority. If a unit appears later, normal readiness and freshness checks apply
before output is enabled.

Pure software bench builds may compile TWAI self-test so ACK is not required.
Self-test is forbidden as a vehicle behavior. A hardware bench without
self-test must provide an active ACK node; a listen-only analyzer does not ACK.

## 4. Bus-Off handling

Bus-Off immediately closes TX admission and clears queued gateway traffic.
Automatic recovery is intentionally not initiated by RT, because ESP-IDF can
resume frames retained in the old transmit queue immediately after recovery.

In production, Bus-Off latches ESTOP, zeros motion authority, and requires a
controlled restart after the physical bus and peer startup order are corrected.

In developer bypass, Bus-Off reports CAN unavailable/degraded and leaves TX
closed until restart. It does not synthesize an ESTOP merely because optional
hardware is absent. Any independent physical or received ESTOP remains active.

Required diagnostics include the first TX time, first valid low-peer time, TX
admission transitions, TEC/REC, Bus-Off state, TX queue rejection, and the
specific ESTOP source.

## 5. Power sequencing

The normal order is:

1. SYS powers and enables CAN.
2. RT powers, enables receive, and keeps operational TX closed.
3. SYS transmits heartbeat/mode state; RT receives and ACKs it.
4. RT opens transport TX admission.
5. Required actuators advertise readiness.
6. Authority may transition from safe state to Armed.

A planned unit shutdown should announce its state before leaving the bus.
Unexpected disappearance is handled by message deadlines and safe outputs, not
by assuming that a CAN ACK identifies the intended receiver.
