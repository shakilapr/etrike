# Software-in-the-Loop (SIL) Testing Guide

Even with the current custom Web UI, you can perform highly effective Software-in-the-Loop (SIL) testing without any physical hardware. By interfacing directly with the Debug Tool's backend, you can simulate ECUs (like the Jetson HOST, RT, and MTR) using Python test scripts.

## Why SIL Testing?
- **Zero Hardware Required:** Tests run purely on your PC.
- **Automated CI/CD:** You can integrate these scripts into your pipeline to run on every commit.
- **Edge Cases:** Safely test critical fault conditions (e.g., ESTOP, broken heartbeat, checksum corruption) that are difficult or dangerous to trigger on physical hardware.

## How It Works (The Architecture)
The Debug Tool backend exposes both a **REST API** and an **Embedded MQTT Broker**. You can write Python scripts to act as "simulated ECUs" that publish and subscribe to these interfaces.

```text
[ Python SIL Script (pytest) ]
          │      ▲
  (REST POST)  (MQTT Sub / REST GET)
          ▼      │
[ Node.js Backend (Fastify + Aedes) ]
          │
      (WebSocket)
          ▼
[ Svelte Web UI (Monitor & Dash) ]
```

## Option 1: MQTT-Based SIL (Recommended for Real-Time Models)
Since the backend runs an embedded MQTT broker (Aedes) on port `1883`, Python scripts can connect and inject/listen to CAN frames directly. This perfectly mimics the physical CAN bus behavior in software.

### Setup
```bash
pip install paho-mqtt
```

### Example: Simulating the RT Controller
This Python script listens for HOST drive commands (0x300) from the UI and immediately replies with RT state (0x210) to the UI.

```python
import paho.mqtt.client as mqtt
import json
import time

def on_connect(client, userdata, flags, rc):
    print("SIL Simulator Connected")
    client.subscribe("etrike/debug/cmd/send")

def on_message(client, userdata, msg):
    payload = json.loads(msg.payload.decode())
    
    # If we receive a HOST_DRIVE_CMD (0x300) from the UI
    if payload.get("id") == "0x300":
        print("Received drive command, generating RT response...")
        
        # Simulate RT sending 0x210 (RT_STATE_RPT)
        response_frame = {
            "bus": "high",
            "id": "0x210",
            "dlc": 4,
            "data": [0x01, 0x00, 0x00, 0x00], # AUTO Mode, No Faults
            "ts": time.time()
        }
        client.publish("etrike/debug/can/rx/high/0x210", json.dumps(response_frame))

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("127.0.0.1", 1883, 60)
client.loop_forever()
```

## Option 2: REST API-Based SIL (Great for One-Shot Tests)
For simple behavioral tests (like verifying the backend state correctly updates when an ESTOP occurs), use the REST API.

### Setup
```bash
pip install requests pytest
```

### Example: Testing UI ESTOP Reaction
```python
import requests
import time

BASE_URL = "http://127.0.0.1:3000/api"

def test_estop_injection():
    # 1. Inject an ESTOP frame (0x001)
    payload = {
        "bus": "high",
        "id": "0x001",
        "dlc": 0,
        "data": [],
        "confirm_estop": True
    }
    res = requests.post(f"{BASE_URL}/cmd/send", json=payload)
    assert res.status_code == 200
    
    # 2. In a real SIL test, you would then poll the API 
    # to verify that the virtual RT controller halted the virtual motors.
    time.sleep(0.5)
    
    # Check the latest frames to ensure drive speed went to 0
    latest = requests.get(f"{BASE_URL}/can/latest").json()
    assert latest["latest"]["high:0x300"]["data"][0] == 0 # Example check
```

## Running the Tests
1. Start the Debug Tool in MQTT or Disabled mode:
   ```bash
   cd debug-tool/backend
   CAN_TRANSPORT=mqtt npm run dev
   ```
2. Run your Python SIL scripts in a new terminal:
   ```bash
   pytest test_sil.py
   # OR
   python simulate_rt.py
   ```
3. Watch the Svelte Web UI to see the automated tests interact and populate the dashboard in real-time.
