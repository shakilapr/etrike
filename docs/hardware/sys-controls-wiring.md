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

| Function | GPIO | Switch |
|----------|------|--------|
| Brake lever | GPIO2 | Momentary NO |
| START | GPIO41 | Momentary NO |
| MODE | GPIO11 | Momentary NO |
| Bypass | GPIO42 | Latching, closed at power-up = bypass ON |
| Left | GPIO9 | (not wired) |
| Right | GPIO6 | (not wired) |
| Headlight | GPIO7 | (not wired) |

## ESTOP switch

```
GPIO1 ── NC contact ── GND
```

Released = normal. Pressed or wire broken = ESTOP.

No external resistor needed: the firmware enables the internal pull-up on
GPIO1. Do not add a pull-down and do not feed 3.3 V into GPIO1 — that wiring
belongs to the older hardware docs and would read as permanent ESTOP against
this firmware.

## Components needed

None — every input uses the firmware internal pull-up, and the relay module
contains the drivers. No external resistors anywhere.

Never connect +12 V to any ESP32 GPIO.
