# Bug analysis: RT Low CAN missing while High / toolkit OK

**Date:** 2026-07-22  
**Mode:** Real · CANalyst-II · `bench_test` · Bench TX enabled  
**API:** `http://127.0.0.1:8001/api/v1`  
**Serial:** RT COM10, SYS COM6  
**Related:** [`RT-CAN-BENCH-INCIDENT-2026-07-21.md`](RT-CAN-BENCH-INCIDENT-2026-07-21.md)

---

## Summary

| Question | Answer |
|----------|--------|
| Is this a Control Toolkit / framework bug? | **No** |
| Is USB / CANalyst broken? | **No** |
| Is the Low bus fully dead? | **No** (later observation: SYS live on Low) |
| What is broken? | **RT is not transmitting on Low CAN** |

RT High path works. The toolkit correctly observes silence (or partial traffic) on Low and reports **RT_low missing**. Root cause sits on the **RT Low TWAI / transceiver / wiring** path (bus-off on RT serial), not in the USB adapter or API.

---

## Observed symptoms (UI / topology)

- ECU lamps / topology: **RT-H live**, **RT-L missing**, **SYS** offline or live depending on moment, **MTR** offline.
- Live CAN: High `RT_HEARTBEAT 0x7FD` / `RT_STATE_RPT 0x210` live.
- Low: `RT_HEARTBEAT`, `RT_DRIVE_CMD 0x204`, `RT_STATE_RPT`, `RT_BRAKE_CMD` **missing** (ages often many minutes).

---

## USB / adapter (API `GET /api/v1/status`)

Representative snapshot during investigation:

| Field | Value | Interpretation |
|-------|--------|----------------|
| `adapter.identity` | `canalystii` | Physical USB transport |
| `adapter.health` | `active` | Adapter open and working |
| `adapter.worker_alive` | `true` | RX worker running |
| `adapter.last_error` | `null` | No transport fault reported |
| `channel_map` | high=`0`, low=`1` | Correct CH0 High / CH1 Low |
| `bitrate` | `500000` | Expected |
| High `activity` | `active` | Continuous RX |
| High `rx_overflow` / `rx_invalid` | `0` | Clean |
| Low `last_error` | `null` | Channel not in software error |

### Early snapshot (Low quiet)

| Channel | Activity | Notes |
|---------|----------|--------|
| High | active | RX climbing |
| Low | **quiet** | RX count frozen (~693 510); **Δ RX over 2 s = 0** |
| Toolkit Low `tx_count` | **0** | Framework not transmitting on Low this session |

Topology at that time:

| Node | Bus | Liveness |
|------|-----|----------|
| Host | high | offline |
| RT_high | high | **live** |
| RT_low | low | **missing** |
| SYS | low | **missing** |
| MTR | low | offline |

### Later snapshot (Low active, RT still missing)

| Channel | Activity | Δ RX / 2 s |
|---------|----------|------------|
| High | active | +41 |
| Low | **active** | **+145** |

Topology:

| Node | Liveness |
|------|----------|
| RT_high | **live** |
| RT_low | **still missing** |
| SYS | **live** |
| MTR | offline |

State examples:

- High `RT_HEARTBEAT 0x7FD` → live  
- Low `SYS_HEARTBEAT 0x7FE` → live  
- Low `RT_HEARTBEAT 0x7FD` / `RT_DRIVE_CMD` / `RT_STATE_RPT` → missing  

**Conclusion from API:** USB and dual-channel open are fine. When Low RX advances and SYS is live, the Low wire and CANalyst CH1 are proven. Failure is **RT-specific on Low**, not “framework cannot receive Low.”

---

## RT serial (COM10) — root evidence

Repeated while API showed RT-L missing:

```text
E (…) esp_twai: _node_queue_tx(…): node is bus off
W (…) rt: Low CAN TX failed (… state=2 tec=0 rec=0 id=0x204)
W (…) rt: Low CAN RT_STATE_RPT send failed
W (…) rt: Low CAN bus-off — soft recovery
W (…) twai: recovery: bus-off tec=0 rec=0
```

- Low TWAI reports **bus-off**; soft recovery runs then bus-off returns.
- High path continues (matches live High traffic on CANalyst).
- Same class of fault as the 2026-07-21 low-transceiver / low-bus physical issues.

---

## SYS serial (COM6) — secondary

```text
Mode=ESTOP
MTR ESTOP ACK timeout — retriggering ESTOP
TWAI TX=5 RX=4 @ 500 kbit/s
```

- SYS in ESTOP because **MTR does not ACK** (MTR offline / not on bus).
- That is a **separate** safety posture issue.
- Once Low carried SYS heartbeats again, SYS was **not** the reason RT-L stayed missing.

---

## Why this is not a framework bug

| Claim | Evidence against |
|-------|------------------|
| “Toolkit broke Low” | Low `tx_count` = 0; no adapter error; High works |
| “Wrong channel map” | Map high=0 / low=1; High RT IDs appear on High as expected |
| “USB cannot open CH1” | Later Low RX rate positive; SYS live on Low |
| “Topology false negative” | Matches raw state ages and missing Low RT IDs |

Framework is **observing** correctly: RT Low frames are not on the bus.

---

## Root cause (working statement)

**RT Low CAN transmit path is failed (TWAI bus-off / electrical / transceiver).**  
RT continues on High (MCP2515). Control Toolkit + CANalyst report that accurately.

Primary ownership:

1. **Hardware:** RT Low transceiver, CAN-H/L, GND, termination, seating.  
2. **Firmware (secondary):** bus-off recovery behavior on Low TWAI if hardware is good but recovery loops.  
3. **Not:** Control Toolkit session, Bench TX gate alone, or frontend ECU lamps.

---

## Recommended fix / verify sequence

1. Power down bench; inspect **RT Low** transceiver, wiring, termination, common ground.  
2. Reseat or swap RT Low transceiver if the fault follows the module (as in prior incident).  
3. Power up; watch RT serial until **Low bus-off spam stops**.  
4. API checks (30 s):
   - Low `activity` active and RX count increasing  
   - Topology **RT_low = live**  
   - State: Low `RT_HEARTBEAT 0x7FD` (and ideally `RT_STATE_RPT 0x210`) **live**  
5. Optional: MTR present or expected-absent for SYS ESTOP ACK (separate from RT-L TX).

---

## Quick API checklist

```powershell
$base = 'http://127.0.0.1:8001/api/v1'
$s = Invoke-RestMethod "$base/status"
$s.adapter.channels | ConvertTo-Json -Depth 5
(Invoke-RestMethod "$base/topology").nodes | Format-Table node, bus, liveness, freshness
# After 2s, compare low.rx_count delta — should be > 0 if any Low traffic
# RT-L healthy: topology RT_low liveness == live
```

---

## Status

| Item | Status |
|------|--------|
| Framework bug | **Rejected** |
| USB / channel map | **OK** |
| RT Low TX | **Failed / under hardware investigation** |
| SYS ESTOP without MTR | **Expected when MTR absent; separate** |
| Doc purpose | Capture evidence so the issue is not re-triaged as toolkit/USB |
