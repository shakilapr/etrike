# CANalyst-II Setup Checklist — Windows

Getting the CANalyst-II dual-channel USB CAN analyzer working with the E-Trike debug tool. Follow every step in order.

---

## Hardware

- [ ] **CANalyst-II** USB analyzer (Chuangxin Tech, VID `0x04D8` / PID `0x0053`)
- [ ] USB cable (USB-B on the CANalyst-II side, USB-A on the PC side)
- [ ] DB9 CAN cables × 2 (channel 0 → high bus, channel 1 → low bus)
- [ ] 120Ω termination resistors (if not built into the CAN wiring harness)

---

## Step 1 — Driver Binding

The CANalyst-II ships with a proprietary driver. You must replace it with **WinUSB** or **libusbK** so that `pyusb` can talk to it.

### Option A: Signed Installer (recommended, one-click)

Run the pre-built signed driver installer:

```
tools\canalystii-driver\generated\installer_x64.exe
```

1. Plug in the CANalyst-II
2. Run `installer_x64.exe`
3. Follow the prompts
4. Unplug and re-plug the device

> This installs libusbK via a signed INF/CAT package. No need to disable driver signature enforcement.

### Option B: Zadig (manual, more control)

Run Zadig:

```
tools\canalystii-driver\zadig-2.9.exe
```

1. Plug in the CANalyst-II
2. In Zadig: **Options → List All Devices**
3. Select **"Chuangxin Tech USBCAN/CANalyst-II"** (or "USB CAN" if unnamed)
4. In the driver dropdown, select **WinUSB** (or libusbK)
5. Click **Replace Driver**
6. Wait for confirmation, then unplug/re-plug

> If the device doesn't appear: check **Options → Advanced Mode**, then re-check **List All Devices**.

### Verify Driver Binding

Open **Device Manager** → expand **Universal Serial Bus devices**. You should see:

```
WinUSB Device (Chuangxin Tech USBCAN/CANalyst-II)
```
or
```
libusbK USB Devices → Chuangxin Tech USBCAN/CANalyst-II
```

If it still says "USB CAN" under "Other devices", the driver binding didn't take — retry.

---

## Step 2 — Python (on PATH)

The backend spawns `canalystii_bridge.py` via `python`. The `python` command must resolve on PATH.

```powershell
# Verify:
python --version
# Must print: Python 3.11.9  (or later)
```

If `python` opens the Microsoft Store instead, you need to **disable the App Execution Alias**:

1. Open **Windows Settings** → **Apps** → **Advanced app settings** → **App execution aliases**
2. Turn **OFF** `python.exe` and `python3.exe`
3. Restart your terminal

If Python 3.11 is not installed:

```powershell
winget install Python.Python.3.11 --silent
# Then disable the App Execution Alias as above.
```

> The backend uses `CANALYST_PYTHON=python` by default. If your Python is at a non-standard path, set `CANALYST_PYTHON` to the full path (e.g., `C:\Users\ASUS\AppData\Local\Programs\Python\Python311\python.exe`).

---

## Step 3 — Python Libraries

Install the two Python packages the bridge script depends on:

```powershell
pip install canalystii pyusb
```

Verify:

```powershell
python -c "import canalystii; print('canalystii OK')"
python -c "import usb; print('pyusb OK')"
```

| Package | Version | Purpose |
|---------|---------|---------|
| `pyusb` | ≥ 1.2.0 | USB device access via libusb/WinUSB backend |
| `canalystii` | ≥ 0.1 | CANalyst-II USB protocol (open/init/send/receive) |

> Already installed on this machine? Run `pip show canalystii pyusb` to check.

---

## Step 4 — Backend Dependencies

The debug tool backend spawns the Python bridge as a child process. Install Node.js packages:

```powershell
cd debug-tool\backend
npm install
```

Verify the bridge script exists:

```powershell
# From debug-tool\backend:
Test-Path canalystii_bridge.py   # must return True
```

---

## Step 5 — Run

Plug in the CANalyst-II, then:

```powershell
cd debug-tool\backend
$env:CAN_TRANSPORT = "canalystii"
$env:CANALYST_BITRATE = "500000"
npm run dev
```

Expected output in the terminal:

```
{"type":"status","adapter_connected":true,"online":true,"adapter":"CANalyst-II",...}
{"ts":...,"bus":"high","id":"0x7FD","dlc":8,"data":[...]}
{"ts":...,"bus":"low","id":"0x201","dlc":8,"data":[...]}
...
```

Then open the UI in another terminal:

```powershell
cd debug-tool\ui
npm run dev
# → http://127.0.0.1:5173
```

---

## Environment Variables Reference

| Variable | Default | Purpose |
|----------|---------|---------|
| `CAN_TRANSPORT` | `serial` | Must be set to `canalystii` |
| `CANALYST_BITRATE` | `500000` | CAN bus bitrate (500 kbit/s for both buses) |
| `CANALYST_POLL_MS` | `5` | USB poll interval in ms |
| `CANALYST_DEVICE_INDEX` | `0` | Device index (0 = first CANalyst-II found) |
| `CANALYST_CH0_BUS` | `high` | Channel 0 → which E-Trike bus |
| `CANALYST_CH1_BUS` | `low` | Channel 1 → which E-Trike bus |
| `CANALYST_PYTHON` | `python` | Python executable (change if `python` not on PATH) |

Set them inline (PowerShell):

```powershell
$env:CAN_TRANSPORT = "canalystii"
$env:CANALYST_BITRATE = "500000"
```

Or persist them in `debug-tool\backend\.env`:

```
CAN_TRANSPORT=canalystii
CANALYST_BITRATE=500000
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `canalystii_bridge.py not found` | Wrong working directory | Run from `debug-tool/backend/` |
| `python: command not found` | Python not on PATH | Fix App Execution Alias or set `CANALYST_PYTHON` to full path |
| `ModuleNotFoundError: canalystii` | pip package missing | `pip install canalystii` |
| `usb.core.NoBackendError` | libusb DLL missing or driver not bound | Run Zadig again, verify WinUSB binding in Device Manager |
| `CanalystDevice: device not found` | Device not plugged in or wrong driver | Check USB cable, check Device Manager |
| `Access denied (insufficient permissions)` | WinUSB binding issue | Re-run Zadig as Administrator |
| Bridge exits with code 1 | CANalyst-II init failed | Check bitrate, check cables, check device is plugged in |
| Backend connects but no frames | CAN bus is silent | Verify other ECUs are powered and transmitting; check CAN wiring |

---

## Data Path (Reference)

```
CAN bus (500 kbit/s)
  │
  ▼
CANalyst-II hardware (2× MCP2515 + USB MCU)
  │  USB 2.0 Full-Speed
  ▼
WinUSB / libusbK kernel driver
  │  IOCTL
  ▼
pyusb (Python, libusb-1.0 backend)
  │  bulk transfers
  ▼
canalystii (Python, protocol framing)
  │  Message objects
  ▼
canalystii_bridge.py (Python child process)
  │  JSON over stdout (one line per frame)
  ▼
CanalystBridge (Node.js/TypeScript, child_process.spawn)
  │  WebSocket broadcast
  ▼
Svelte UI (browser, http://127.0.0.1:5173)
```

---

## Files in This Repo

| File | Purpose |
|------|---------|
| `tools/canalystii-driver/zadig-2.9.exe` | USB driver binding tool |
| `tools/canalystii-driver/zadig.ini` | Zadig config (WinUSB default) |
| `tools/canalystii-driver/canalystii-zadig-preset.cfg` | Device preset (VID 04D8 / PID 0053) |
| `tools/canalystii-driver/generated/installer_x64.exe` | Signed driver installer (one-click) |
| `tools/canalystii-driver/generated/Chuangxin_Tech_USBCANCANalyst-II.inf` | Windows driver INF |
| `tools/canalystii-driver/generated/Chuangxin_Tech_USBCANCANalyst-II.cat` | Signed catalog |
| `tools/canalystii-driver/generated/amd64/libusb0.dll` | libusb-win32 x64 |
| `tools/canalystii-driver/generated/amd64/libusbK.dll` | libusbK x64 |
| `tools/canalystii-driver/generated/amd64/libusb0.sys` | libusb-win32 kernel driver x64 |
| `tools/canalystii-driver/generated/amd64/libusbK.sys` | libusbK kernel driver x64 |
| `debug-tool/backend/canalystii_bridge.py` | Python bridge (CAN ↔ JSON, spawned by backend) |
| `debug-tool/backend/src/canalyst/bridge.ts` | TypeScript bridge manager (spawns Python) |
| `debug-tool/backend/.env.example` | Environment variable template |
