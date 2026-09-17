# SYS ESP32-S3 → 12 V Relay Wiring

Relay: 12 V 8-ch, active-LOW (IN LOW = ON). If yours is active-HIGH, skip the ULN and connect GPIO → IN direct.

## Power

```
+12 V (fused) → Relay VCC
GND common: Relay GND = ULN2803 GND = ESP32 GND = lamp −
```

## Outputs: ESP32 → ULN2803A → Relay Module (Pin-to-Pin & Screw Terminals)

| Relay Ch | Signal Name (Code) | ESP32 Pin | ULN2803A | Screw Terminals (COM / NO / NC) | Connected Load (+12 V) |
|:---:|---|:---:|:---:|---|---|
| **K1** | AUTO (`kBulbAuto`) | **GPIO 48** | 1 → 18 | `COM: +12V` \| `NO: Output` \| `NC: Open` | 12 V AUTO Mode Lamp (+) |
| **K2** | MANUAL (`kBulbManual`) | **GPIO 39** | 2 → 17 | `COM: +12V` \| `NO: Output` \| `NC: Open` | 12 V MANUAL Mode Lamp (+) |
| **K3** | READY (`kBulbReady`) | **GPIO 17** | 3 → 16 | `COM: +12V` \| `NO: Output` \| `NC: Open` | 12 V READY Green Lamp (+) |
| **K4** | *Reserved (Native USB D+)* | **GPIO 20** | 4 → 15 | **Leave All Terminals Open** | **DO NOT USE (Native USB D+)** |
| **K5** | BYPASS (`kBulbBypass`) | **GPIO 14** | 5 → 14 | `COM: +12V` \| `NO: Output` \| `NC: Open` | 12 V BYPASS Amber Lamp (+) |
| **K6** | ESTOP (`kBulbEstop`) | **GPIO 18** | 6 → 13 | `COM: +12V` \| `NO: Output` \| `NC: Open` | 12 V ESTOP Red Lamp (+) |
| **K7** | *Reserved (Native USB D-)* | **GPIO 19** | 7 → 12 | **Leave All Terminals Open** | **DO NOT USE (Native USB D-)** |
| **K8** | Brake (`kLightBrake`) | **GPIO 21** | 8 → 11 | `COM: +12V` \| `NO: Output` \| `NC: Open` | 12 V Rear Brake Light (+) |

- **Terminal Rules:** `COM` = +12 V (Fused 5 A Bus); `NO` = Switched +12 V feed to Lamp (+); `NC` = Leave open/unused.
- **Ground & Supply:** All lamp negative (−) leads return to Common GND (0 V). ULN Pin 9 → GND; Pin 10 → +12 V Fused Rail.

> **USB Pin Conflict Warning (GPIO 19 & GPIO 20):**
> Relays K4 (GPIO 20) and K7 (GPIO 19) are connected to the ESP32-S3's Native USB data lines (`D+` and `D-`). **Leave Relay K4 and K7 completely disconnected (NO, COM, NC all open)!** If a USB cable is connected, USB data signals will rapidly chatter these relays.

```
ESP32 GPIO (3.3V) ──► ULN2803A Sink ──► Relay Module (Active-LOW) ──► Relay NO ──► 12V Lamp (+)
+12V (Fused Bus)  ──────────────────────────────────────────────────► Relay COM
Common GND (0V)   ────────────────────────────────────────────────────────────────► Lamp (-)
```

## Switches (wire to GND)

Every input uses the ESP32-S3 internal pull-up to 3.3 V. All switches are dry contacts wired between the GPIO pin and **GND**.

| Function | GPIO | Switch / Contact Type | Operation / Logic |
|----------|------|-----------------------|-------------------|
| Brake lever | GPIO2 | Momentary NO (Microswitch) | Pull lever → Closes to GND (Active-LOW) |
| START | GPIO41 | Momentary NO (Push Button) | Press → Closes to GND (Exits ESTOP → MANUAL) |
| MODE | GPIO11 | Momentary NO (Push Button) | Press → Closes to GND (Toggles MANUAL ↔ AUTO; 3s hold exits ESTOP) |
| Bypass | GPIO42 | Latching / Jumper | Closed to GND at boot = Developer bench bypass ON |
| Left Turn | GPIO9 | Handlebar Switch (NO/Toggle) | Press → Closes to GND (Toggles Left Blinker in MANUAL) |
| Right Turn | GPIO6 | Handlebar Switch (NO/Toggle) | Press → Closes to GND (Toggles Right Blinker in MANUAL) |
| Headlight | GPIO7 | Handlebar Switch (Toggle) | Press → Closes to GND (Toggles Headlight in MANUAL) |

## ESTOP switch

```
GPIO1 ──── [ NC Red Mushroom Switch ] ──── GND
```

- **Type:** Red mushroom emergency stop button with **NC (Normally Closed)** latching contact.
- **Released / Healthy:** Contact is CLOSED to GND (reads LOW / 0 V) → Normal operation allowed.
- **Pressed / Open Circuit:** Contact OPENS → Internal pull-up pulls GPIO1 to 3.3 V (HIGH) → Immediate latched ESTOP.
- **Fail-Safe:** Any disconnected wire or cut cable automatically pulls HIGH and trips ESTOP.

No external resistor needed: the firmware enables the internal pull-up on
GPIO1. Do not add a pull-down and do not feed 3.3 V into GPIO1 — that wiring
belongs to older obsolete drawings and would read as permanent ESTOP against
this firmware.

## Components needed

None — every input uses the firmware internal pull-up, and the relay module
contains the drivers. No external resistors anywhere.

**Never connect +12 V to any ESP32 GPIO.**
