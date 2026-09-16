# SYS ESP32-S3 → 12 V Relay Wiring

Relay: 12 V 8-ch, active-LOW (IN LOW = ON). If yours is active-HIGH, skip the ULN and connect GPIO → IN direct.

## Power

```
+12 V (fused) → Relay VCC
GND common: Relay GND = ULN2803 GND = ESP32 GND = lamp −
```

## Outputs: ESP32 → ULN2803A → Relay IN

| ESP32 | ULN | Relay | Function |
|-------|-----|-------|----------|
| GPIO48 | IN1→OUT1 | IN1/K1 | AUTO |
| GPIO39 | IN2→OUT2 | IN2/K2 | MANUAL |
| GPIO17 | IN3→OUT3 | IN3/K3 | READY |
| GPIO20 | IN4→OUT4 | IN4/K4 | *(Unused - USB conflict)* |
| GPIO14 | IN5→OUT5 | IN5/K5 | BYPASS amber |
| GPIO18 | IN6→OUT6 | IN6/K6 | **ESTOP red** |
| GPIO19 | IN7→OUT7 | IN7/K7 | *(Unused - USB conflict)* |
| GPIO21 | IN8→OUT8 | IN8/K8 | Brake lamp |

> **Special Note for Side Signals / Unused Pins:**
> Relays K4 (GPIO20) and K7 (GPIO19) are connected to the ESP32-S3's Native USB data lines (D+ and D-). The side signal lamps and old ESTOP routing have been removed from the firmware. **Do not connect any side lamps or loads to Relay K4 or K7 while using USB!** If a USB cable is connected, the USB data signals will rapidly toggle these relays.

```
ESP32 GPIO → ULN INx
ULN OUTx → Relay INx
ULN COM: not used
```

GPIO40: not used, leave unconnected.

## Lamps

```
+12 V (fused) → Relay COM
Relay NO → Lamp +
Lamp − → GND
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
