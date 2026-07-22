# CANalyst-II setup and connection gate

The Control Toolkit physical transport uses the dual-channel **CANalyst-II USB
adapter** through `python-can`. It does not require Vector CANalyzer software.
The mapping is fixed and tested in software:

| CANalyst channel | E-Trike bus | Bitrate |
|---|---|---:|
| CH0 | High | 500 kbit/s |
| CH1 | Low | 500 kbit/s |

There is no automatic physical-to-virtual fallback. Physical sessions start
with Bench TX disabled, and an adapter failure clears jobs and leases. A
reconnected adapter returns in receive-only mode until the operator explicitly
enables Bench TX again.

If by “CANalyzer” you mean **Vector CANalyzer**, the Diagnostics recording table
has **Export CANalyzer**. It downloads a ZIP containing a standard `.blf`, the
generated High/Low `.dbc` files, and a metadata sidecar. This export works in
Computer or Real mode and does not change the runtime transport.

## Connection stages

Do not connect everything at once. Pass each stage in order.

### Stage 1 — software only (adapter disconnected)

From `control-toolkit/backend`:

```powershell
python -m pip install -e ".[dev]"
python -m pytest -q
python scripts\canalyst_preflight.py
```

With no adapter, the final command should identify USB `04D8:0053` as missing.
That is an expected preflight result, not a reason to connect the vehicle yet.

### Stage 2 — connect USB only

Connect the CANalyst-II to the PC using USB. Leave both DB9/CAN connections
disconnected. On Windows, the device must use a PyUSB-compatible driver such as
WinUSB/libusbK. Repository tools are available at:

```text
tools\canalystii-driver\generated\installer_x64.exe
tools\canalystii-driver\zadig-2.9.exe
```

Then run the passive dual-channel open test:

```powershell
cd e:\work\etrike\control-toolkit\backend
python scripts\canalyst_preflight.py --open --listen-seconds 3
```

Required result:

- `stage` is `dual_channel_open`, followed by `ready_for_unpowered_can_wiring`;
- `transmitted` is `0`;
- adapter health is `open` or `quiet`; and
- `channel_map` is `high: 0`, `low: 1`.

If the probe fails, do not attach CAN wiring. Check Device Manager, the USB
driver binding, cable, and the `canalystii`/`pyusb` packages first.

### Stage 3 — wire the unpowered bench/vehicle

1. Stop the backend and switch off the bench/vehicle.
2. Connect CANalyst **CH0** to the E-Trike **High** bus.
3. Connect CANalyst **CH1** to the E-Trike **Low** bus.
4. Connect the required CAN ground/reference according to the adapter and
   harness documentation.
5. Verify CAN-H/CAN-L polarity and correct termination for each bus. Do not add
   another terminator if the complete harness is already terminated.

The Control Toolkit cannot verify polarity, grounding, isolation, or
termination in software.

### Stage 4 — power and observe, still no TX

Power the ECUs, then run:

```powershell
$env:CTK_PHYSICAL = "1"
$env:CTK_REQUIRE_CAN_TRAFFIC = "1"
python -m pytest tests\test_hw_characterization.py -v -m hardware
```

Or start the application, open **Settings**, refresh until the adapter is
detected, and select **Real → Full Vehicle**. Do **not** enable Bench TX. Confirm:

- adapter health becomes `active` or `quiet`;
- High and Low RX counters increase when those buses are active;
- no RX overflow or invalid frame count grows; and
- Live CAN shows expected IDs on the expected bus.

### Stage 5 — controlled bench transmission

Only use **Real → Bench Test** after the physical bench is safe, channel mapping
has been observed, actuators are secured, and an operator can remove power.
Enable Bench TX only for the intended test, start with neutral/zero commands,
and use **Stop All** before changing wiring or profile.

## Environment overrides

| Variable | Default |
|---|---:|
| `CTK_CANALYST_DEVICE_INDEX` | `0` |
| `CTK_CANALYST_BITRATE` | `500000` |
| `CTK_CANALYST_POLL_MS` | `2` |
| `CTK_CANALYST_RECEIVE_TIMEOUT_MS` | `100` |
| `CTK_CANALYST_RECONNECT_INITIAL_MS` | `250` |
| `CTK_CANALYST_RECONNECT_MAX_MS` | `5000` |
| `CTK_CANALYST_RECOVERY_STABILITY_MS` | `500` |

## Important limitations

- CANalyst-II is Classical CAN only; CAN FD is not supported by this backend.
- Frames are grouped by USB channel. Cross-channel ordering uses backend
  arrival time and can include USB polling jitter.
- The backend cannot provide trustworthy bus-off, TEC/REC, TX echo, or
  listen-only evidence. The UI reports those capabilities as unknown.
- A successful `send()` means submitted to the adapter, not acknowledged by an
  ECU or delivered to an actuator.
