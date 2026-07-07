# Debug Tool — Unsolved Known Bugs

> Severity: **P0** = data loss / safety confusion, **P1** = wrong behavior, **P2** = cosmetic / edge case.
> Note: Solved bugs have been cleaned from this list. Refer to `checklist.md` for the full historical checklist.

---

## P0 — Critical

No active P0 bugs currently tracked here.

## P1 — Wrong Behavior

No active P1 bugs currently tracked here.

## P2 — Cosmetic / Edge Cases

- **BUG-05:** Serial port fails silently (UI shows error, just no backend console log).
- **BUG-16:** CANalyst-II bridge abandoned after detection failure can't be reused.
- **BUG-18:** Emulator `simMode` toggle resets when switching tabs.
- **BUG-26:** `SerialBridge.start()` called twice on reconnect (Double open error).
- **BUG-29:** `stopRecording()` does not prevent double-stop.
- **BUG-30:** `normalizeBus()` falls through to `"high"` silently.
- **BUG-34:** `normalizeFilter` strips bus from bare IDs.
- **BUG-36:** Controller `tick()` reads `heldNow` via Svelte reactive assignment (stale closure edge case).
- **BUG-54:** `Topbar.svelte` reports the USB port state as "open" when disconnected instead of "closed" or "offline".
- **BUG-55:** `Dashboard.svelte` hardcodes the obsolete `"EPS-C"` string for steering instead of the updated `"SES"`.
