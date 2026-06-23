# debug-esp32 — ESP32-S3 CAN-to-MQTT Bridge

Reads CAN bus traffic via TWAI (and optional MCP2515), publishes decoded frames
as JSON over MQTT to the debug backend.

## Hardware

- ESP32-S3 DevKit
- SN65HVD230 CAN transceiver (GPIO 5=TX, 4=RX)
- Optional: MCP2515 SPI CAN controller (GPIO 36–40) for dual-bus

## Build & Flash

```bash
pio run -t upload -t monitor
```

## Wi-Fi & MQTT Config

Set via `sdkconfig.defaults` or menuconfig:

```
CONFIG_DEBUG_WIFI_SSID="your-ssid"
CONFIG_DEBUG_WIFI_PASSWORD="your-password"
CONFIG_DEBUG_MQTT_BROKER="192.168.1.100"
```

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `etrike/debug/can/rx/<bus>` | ESP32 → Backend | CAN frame JSON |
| `etrike/debug/can/stats` | ESP32 → Backend | Per-bus stats (1 Hz) |
| `etrike/debug/status` | ESP32 → Backend | Online/offline heartbeat (5 s) |
| `etrike/debug/uptime` | ESP32 → Backend | Uptime seconds |
| `etrike/debug/cmd/send` | Backend → ESP32 | Single CAN injection |
| `etrike/debug/cmd/send/periodic` | Backend → ESP32 | Periodic CAN injection |
| `etrike/debug/cmd/response` | ESP32 → Backend | Command acknowledgment |
