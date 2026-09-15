# DEPRECATED — legacy direct-CAN system bench

This suite is **superseded** by `control-toolkit/backend/tests/hw_bench/`.

Why it is no longer used:

* It opens the **CANalyst-II directly** via `python-can`
  (`bench/can.py: CanalystLink`). The control-toolkit backend is the sole owner
  of the USB adapter now; a second opener fails with `Errno 13`
  ("Access denied") on Windows.
* Its `restbus/` fakes **SES / SEB / MTR** feedback over CAN
  (`restbus/ses.py`, `restbus/seb.py`). Current bench directives forbid
  synthesising absent-actuator feedback; the backend runs in developer bypass
  mode and missing peers must not trip a safety stop.

Use instead:

```powershell
cd control-toolkit/backend
pytest tests/hw_bench -v
```

Runbook and rationale: `docs/testing_and_validation/hardware-bench-suite-guide.md`.

The code is kept for historical reference only (its scenario ideas and temporal
assertions may be ported to the REST-only suite later).
