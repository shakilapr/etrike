> **Status note (2026-09-08):** this scenario analysis tracks the implemented ESTOP
> architecture. Items still deferred/open per docs/working-architecture.md + docs/estop.md:
> 0x001 sender/reason/epoch payload (issue #11), an RT positive 0x011 acquisition gate (#14),
> distinct brake source IDs (issue #3 option-3), real wheel-speed supervision (0x122, hardware),
> and IWDG on nodes without an independent watchdog. NODE_STATUS 0x500/0x501/0x502 are now
> emitted by all three ECUs (observational only).

# ESTOP & safety scenarios — root-cause analysis

Deep analysis of 18 failure scenarios against the actual firmware
(SYS `sys-esp32`, RT `rt-esp32`, MTR `mtr-stm32`, RM `rm-esp32`, SEB codec).
Each scenario is traced to a root cause, the logic is broken down, and the
optimal solution is given — tagged **[FIXED]**, **[SOFTWARE-fixable]**, or
**[HARDWARE/SEB/PROTOCOL]** so effort goes to the right place.

Companion docs: `docs/estop.md` (authoritative ESTOP flow + reset playbook),
`docs/working-architecture.md` §8 (safety-issue status).

---

## The 5 root causes

Almost every scenario collapses into one of five structural root causes:

| Root cause | One-line | Scenarios |
|-----------|----------|-----------|
| **RC1** | Every stop is a *latched, operator-cleared* ESTOP; `0x001` is a broadcaster-only, no-origin trigger | 1, 2, 3, 18 |
| **RC2** | SYS's own safety-critical task death is only *logged*, never acted on; RT keys brake-fallback off heartbeat (a different task) | 8, 9, 10, 11 |
| **RC3** | No independent hardware stop (no MTR/SYS IWDG, no external WDT, button polled in software) | 4, 5 |
| **RC4** | MTR REARM edge sequencing has two asymmetric corner cases (carry-across vs. never-observed-OFF) | 12, 13 |
| **RC5** | Authority/validity + measurement gaps: RT no positive 0x011 acquisition gate; no physical speed; docs drift | 6, 7, 14, 15, 16, 17 |

---

## Scenario-by-scenario analysis

### 1. ESTOP activated once → cannot reset; ESTOP immediately comes back
- **Root cause:** RC1. SYS latches `Mode::Estop` on any trip and clears **only** via
  START / 3 s MODE (`sys-esp32/src/mode_manager.cpp:15-48,78`). If the underlying
  condition is still asserted (button still high, RT/MTR still asserting, a peer
  still sending `0x001`, SEB L3 still set), any reset is immediately re-latched.
- **Logic:** every `force_estop()` entry point re-broadcasts `0x001`
  (`main.cpp:315,348,373,449,507,569,601,624,1078`) and the receiving nodes
  re-latch. There is no per-source "one-shot" — a single persistent assertor holds
  the whole bus stopped.
- **Optimal:**
  - **[SOFTWARE]** Make the reset **cause-gated**: `task_mode` already refuses to
    clear `kLatchedSebL3`/`kLatchedBrakeFollowing` while the cause is active
    (`main.cpp:753-776`). Extend the same gating to the button (SYS should not
    clear while its own ESTOP GPIO reads active) — button clearing is already
    inherently gated because releasing the button is required for the mode to
    stay out of ESTOP (the button re-triggers `task_safety`).
  - **[SOFTWARE]** Surface *why* reset failed to the operator (blink the ESTOP
    bulb / log the active latch bit) so "comes back" is diagnosable.
  - **[PROTOCOL]** Add a persistent reason/epoch (issue #11) so a stale assertor
    can be distinguished from a new one.

### 2. ESTOP reset loop: brake max + traction dead even after fault is gone
- **Root cause:** RC1 + RC2 interaction. SYS stays latched (or RT/SYS brake
  ownership is ambiguous) so `0x7B9` holds max stroke and MTR stays powered off.
- **Logic:** SYS `task_brake` emits max stroke while `estop` (`brake_control.h:102-108`);
  SYS stays in ESTOP until reset; if the reset "sticks" (cause gone) but a peer
  (RT fallback or a stale SYS task) re-asserts, the loop repeats.
- **Optimal:** ensure a *single* owner of the persistent stop (SYS) and that all
  `0x001` re-broadcasts stop once SYS is latched (SYS already only broadcasts on a
  *state transition* into ESTOP — `if (g_mode_mgr.mode() != Estop)` guards).
  Root fix is RC1 cause-gating + RC2 (SYS task-death reaction, **[FIXED below]**)
  so the brake producer is never ambiguous.

### 3. 200 ms MTR CAN glitch → full physical-reset-required ESTOP
- **Root cause:** RC1. A single lost frame / transient bus error is treated as a
  hard, latched ESTOP (bus-off ≥5 → `force_estop()`, `main.cpp:1073-1083`; or a
  one-shot `0x001`), with no grace/re-acquisition before latching.
- **Logic:** MTR's comms deadman (500 ms any-frame) is *recoverable*, but SYS's
  bus-off and any received `0x001` are *latched*. There is no "N consecutive
  errors before latch" filter on `0x001`/bus-off at the authority.
- **Optimal:**
  - **[SOFTWARE]** Require **confirmed** (debounced) bus-off before ESTOP: SYS
    already counts `bus_off_count >= 5` but resets it to 0 on any healthy sample
    (`main.cpp:1076-1083`); the residual is a single `0x001` from a flaky peer.
  - **[PROTOCOL]** Give `0x001` a source/epoch so a single transient assert can be
    reconciled; otherwise a single glitchy frame is inherently a stop.

### 4. MTR firmware freezes → throttle/relays may stay energized despite ESTOP
- **Root cause:** RC3. MTR has **no IWDG/WWDG**
  (`mtr-stm32/Core/Inc/stm32g4xx_hal_conf.h:49,68` — commented out) and no
  external hardware enable. If the MCU hangs, the code that evaluates the CAN
  watchdog / DAC-zero never runs; GPIOs hold their last state.
- **Logic:** MTR's fail-safe (`motor_manager.h:318-327`) is *software*. A hang
  skips it. CAN `0x001`/`0x113=OFF` only work if the RX ISR + loop still run.
- **Optimal:** **[HARDWARE]** STM32 IWDG + output-safe reset state; where
  practical an external safety relay de-energized on MCU-stop. This is the single
  most important hardware gap.

### 5. SYS firmware freezes → physical ESTOP button not processed
- **Root cause:** RC3. The ESTOP button is polled in `task_safety`
  (`sys-esp32/src/main.cpp:551-577`); a SYS hang stops polling. There is no
  independent hardware stop path and SYS's external WDT toggle is commented out
  (`main.cpp:587` `// g_wdt.tick()`).
- **Logic:** the button only becomes `0x011.estop_active=1`/`0x113=OFF` if
  `task_safety` + `task_mode`/`task_can_tx` run.
- **Optimal:** **[HARDWARE]** re-enable the external WDT (`g_wdt.tick()`) and/or a
  hardware latching stop (button wired to a fail-safe relay/MTR enable that does
  not depend on SYS software). **[SOFTWARE]** RC2 ([FIXED below]) means if
  `task_safety`/`task_mode`/`task_can_tx` die, SYS itself forces ESTOP — but that
  is moot if the *whole* SYS MCU is hung.

### 6. Physical throttle/motor runaway → EGAS reports no mismatch
- **Root cause:** RC5. "EGAS" compares `0x204` setpoint vs `0x206` which carries
  the **applied speed command** (echo), not a physical measurement. No encoder.
  See `docs/working-architecture.md` §8 item 1.
- **Logic:** `cmd == applied` always by construction (`main.cpp:594-597`), so
  DAC-stuck/relay-weld/motor-runaway never produce a mismatch. Field renamed to
  `motor_command_speed_mmps` across protocol + MTR + RT + SYS **[FIXED]** so
  nothing downstream mistakes it for a measurement.
- **Optimal:** **[HARDWARE]** a wheel/motor speed sensor; then EGAS becomes real.
  Until then the check is a command-path consistency check only (and must be
  described as such — now done in code comments and docs).

### 7. Motor commanded but mechanically dead → EGAS still reports agreement
- **Root cause:** RC5 (same as #6). Command-vs-echo agreement is guaranteed even
  when the motor produces no motion (no speed feedback).
- **Optimal:** same as #6 — requires physical speed feedback.

### 8. SYS brake task dies, SYS heartbeat remains → brake commands disappear, no RT takeover
- **Root cause:** RC2. SYS `task_brake` (sole normal `0x7B9` producer) and
  `task_hb` (publishes `0x7FE`) are different tasks. RT's emergency-fallback
  machine keys off **0x7FE heartbeat liveness**
  (`rt-esp32/src/brake_fallback.h:77-97`), so brake-task death with a live
  heartbeat leaves RT in NORMAL while SEB receives nothing.
- **Logic:** SYS task-death is only *logged* (`main.cpp:1040-1046`); nothing
  `force_estop()`s on `task_brake` missing its deadline. RT sees "SYS alive"
  (heartbeat) and never becomes the emergency writer.
- **Optimal:** **[FIXED]** SYS now reacts to the death of its own critical tasks
  (`task_brake`/`task_safety`/`task_dispatch`/`task_can_tx`/`task_mode`) with
  `force_estop()` + `0x001` after ≥2 consecutive missed 1.5 s deadlines
  (`main.cpp:1049-1077`). SYS then holds ESTOP and drives max brake itself, and
  RT's `0x001`-receive path latches it too. The heartbeat-only blind spot is
  removed because brake-task death now *also* trips SYS ESTOP → `0x001` → RT.

### 9. `0x7B9` disappears → SEB behavior unknown (hold/release/failsafe)
- **Root cause:** RC3/RC2 + out-of-repo SEB firmware. When SYS stops producing
  `0x7B9` (task death [#8] or SYS hang [#5]) and RT is not the writer, what SEB
  does on command loss is **not verified** (it could hold, release, or failsafe).
- **Optimal:** **[HARDWARE/SEB]** document/test SEB command-timeout behavior; that
  is a prerequisite for choosing RT emergency-fallback handback semantics
  (issue #3) and for the deferred distinct-source-ID (option 3) target.

### 10. SYS brake task recovers while heartbeat remains broken → two `0x7B9` producers
- **Root cause:** RC2 + RC1. The fallback machine hands back to SYS based on
  heartbeat return + observed `0x7B9` (`brake_fallback.h:109-122`). If the SYS
  brake task recovers (starts sending `0x7B9` again) *before* RT's epoch-guarded
  handback completes, both can write `0x7B9` briefly.
- **Logic:** handback requires `kSebHandbackVerifyFrames` fresh POST-epoch `0x7B9`
  frames (`brake_fallback.h:116-121`) — this is the guard against exactly this,
  but the window is not zero. With RC2 **[FIXED]**, a recovered SYS brake task
  still requires SYS itself to leave ESTOP first (SYS was latched by task-death),
  which narrows but does not eliminate the race (SYS reset can occur while its
  heartbeat task is still broken).
- **Optimal:** **[SEB/PROTOCOL]** distinct source IDs (option 3) is the clean
  elimination. Interim: keep the epoch guard and make the handback also require
  SYS to be *out of ESTOP* (not merely heartbeating) — the current epoch guard
  mostly provides this because SYS only re-publishes normal `0x7B9` after reset.

### 11. Two `0x7B9` producers → CAN errors / bus-off / erratic brake
- **Root cause:** consequence of #8/#10 if they occur; same-CAN-ID dual writers
  cause bit-level collisions or alternating commands.
- **Optimal:** the single-normal-producer model (issue #3) already removes the
  normal dual writer; RC2 **[FIXED]** removes the brake-task-death path; the
  residual emergency handback is epoch-guarded. Full elimination = option 3.

### 12. Reset during a persistent SEB fault → clear/re-latch race
- **Root cause:** RC1 cause-gating gap. SYS refuses to clear `kLatchedSebL3` while
  SEB L3 is active (`main.cpp:753-761`), but a reset that *coincides* with the
  fault clearing then an L3 re-assert can appear as a race.
- **Logic:** the latch-clear only happens when mode leaves ESTOP *and* cause
  healthy; the detector re-latches on the next `0x721` L3 frame. The "race" is
  actually correct behavior (clear only when healthy), but the operator gets no
  feedback on *which* latch blocked the reset.
- **Optimal:** **[SOFTWARE]** report the blocking latch (already logged); the
  design is sound. Consider a minimum-healthy window before accepting reset.

### 13. REARM sequencing case A / B (auto-rearm vs never-move)
- **Root cause:** RC4. See the detailed breakdown below.
- **Optimal:** **[SOFTWARE]** strict post-clear OFF→ON sequencing with a bounded
  re-arm window and a clear diagnostic; see detailed section.

#### REARM logic breakdown (MTR, `motor_manager.h`)
- Assert: `0x011.estop_active==1` → `trigger_estop()` (`:182-190`), which resets
  the clear sequence (`clear_confirm_`, `last_estop_zero_`).
- Clear: two consecutive zero frames whose rolling counter advances by exactly +1
  → `authorized_clear()` (`:192-223`). `authorized_clear` (`:229-243`):
  `estop_active_=false`, invalidates `0x110`/`0x113` authority, sets
  `rearm_required_=true`, and **keeps** `rearm_off_seen_` (the OFF edge seen
  during ESTOP carries across) while clearing `rearm_observed_`.
- Re-arm: on `0x113`, `rearm_off_seen_` set on OFF (`:137`); ON + `mode_valid_`
  sets `rearm_observed_` (`:138-139`). `ignition_on_` requires
  `!rearm_required_ || rearm_observed_` (`:268-269`).
- **Case A (spurious auto-rearm):** because `rearm_off_seen_` is *not* reset on
  clear, if power was already OFF when the latch began and an OFF was observed
  *before/at* latch time, then after the two-frame clear a single 0x113 ON sets
  `rearm_observed_` — but there was no genuine OFF→ON *after* the clear. The
  carry-across is intentional (so the standard SYS flow — hold OFF during ESTOP,
  then ON after clear — re-arms in one edge) but it means the re-arm edge is not
  guaranteed fresh.
- **Case B (never re-arms):** if during ESTOP `rearm_off_seen_` was *false*
  (e.g. power was ON throughout a benign clear, or SYS never drove OFF), then
  after `authorized_clear()` the OFF that seeds `rearm_off_seen_` never comes
  again (power is already ON) → `rearm_required_` never satisfied → MTR is stuck
  inhibited until a power cycle. This is the "looks healthy but never moves".
- **Optimal solution:** make the re-arm require an OFF→ON edge that is **fresh
  after the clear** in *both* cases, by recording the clear moment and, on the
  post-clear ON, requiring that an OFF was observed **after the clear** (not just
  carried across an indeterminate time). Concretely:
  1. In `authorized_clear()`, keep `rearm_off_seen_` ONLY if the OFF was observed
     *during the ESTOP* (it is — power is OFF in ESTOP) — current behavior is
     fine for the normal flow.
  2. Add `rearm_clear_ms_ = now` and, in `tick()`, a **re-arm deadline**: if
     `rearm_required_ && !rearm_observed_` persists > `kRearmTimeoutMs` (e.g.
     10 s) after the clear, raise `MtrRearmSequenceViolation` (already exists) and
     require the operator to cycle power (a clear diagnostic instead of a silent
     stuck state). This converts silent Case B into a diagnosed, operator-recoverable
     state without changing the safe default.
  3. Do **not** auto-take power OFF in MTR (it has no `0x113` authority to force);
     the fix is diagnostic + explicit sequencing.

### 14. RT boots without 0x011 → operates partially before safety authority
- **Root cause:** RC5. RT only latches on `0x011` *loss* (`safety_stream_loss.h`,
  `last==0` is not a loss, `main.cpp:400-408`); it has no **positive** "must have
  seen a valid 0x011 before AUTO motion" acquisition gate. SYS `0x110` alone can
  put RT in AUTO.
- **Logic:** MTR independently blocks traction until `0x011` is valid
  (`safety_state_valid_`, `motor_manager.h:268-269`), so the *vehicle* can't move,
  but RT can believe AUTO and publish `0x204`/steering before it has confirmed SYS
  safety authority.
- **Optimal:** **[SOFTWARE]** add a positive acquisition gate: RT must have seen a
  valid `0x011` (rolling-counter-valid, CRC-ok) at least once before it will
  treat SYS mode as authoritative for motion (treat "never seen 0x011" as
  MANUAL-equivalent until acquired). Low risk; high clarity.

### 15. Faulty ECU spams 0x001 → whole vehicle permanently ESTOP-latched
- **Root cause:** RC1. `0x001` is a broadcaster with no source; SYS rate-limits
  inbound to 2/500 ms (`config.h:102-103`) and re-latches on each, but a
  continuously-spamming peer keeps any operator reset from sticking.
- **Logic:** SYS `force_estop()` on each `0x001` RX (`main.cpp:332-353`); the
  rate limit only bounds the broadcast storm, not the latch persistence.
- **Optimal:** **[PROTOCOL]** add a source ID to `0x001` (or use the persistent
  `0x011` reason bitmap, issue #11) so SYS can (a) identify the assertor and
  (b) ignore repeated identical-source `0x001` while already latched by that
  source. Until then, at minimum SYS should not re-broadcast `0x001` when already
  in ESTOP (already guarded) — the operator isolation is inherent to the design.

### 16. Docs disagree with code
- **Root cause:** the working tree had a half-applied issue-#1 protocol rename
  (protocol/generated + MTR + RT updated, SYS main.cpp not), so SYS did not even
  compile and docs still said "EGAS" / `actual_speed_mmps`.
- **Optimal:** **[FIXED]** completed the SYS consumer rename and re-framed the
  check in code; `docs/estop.md` and `docs/working-architecture.md` §8 now match
  the code. Regenerate/check generated artifacts against contracts
  (`python protocol/tools/protocol.py generate --check`) before committing
  consumer changes.

---

## Summary of fixes applied in this pass

| Item | Change | Status |
|------|--------|--------|
| RC2 | SYS forces ESTOP when its own `task_safety`/`task_brake`/`task_dispatch`/`task_can_tx`/`task_mode` miss ≥2×1.5 s deadlines (`sys-esp32/src/main.cpp:1049-1077`) | **[FIXED]** commit `6ac9fa4` |
| RC5/#1 | SYS `0x206` consumer uses `motor_command_speed_mmps` (matches committed protocol + MTR/RT); EGAS re-framed as command-path check (`sys-esp32/src/main.cpp`) | **[FIXED]** commit `6ac9fa4` |
| #16 | builds green again: SYS, RT, MTR `pio run -e vehicle`; native ctest (tracked) green | verified |

## Fixes requiring hardware / SEB / protocol (not code-fixable here)

- #4/#5 MTR/SYS **independent hardware stop** (IWDG + output-safe reset + external
  WDT re-enable + hardware safety relay). Blocking for freeze scenarios.
- #9 SEB **command-loss behavior** must be documented/tested (holds/releases?).
- #1/#6/#7 real **wheel/motor speed sensor** for genuine EGAS and closed-loop.
- #15/#18 `0x001` **source/reason/epoch** (issue #11) for spam resilience and
  per-origin debounce.
- #11/#3 option-3 **distinct brake source IDs** (needs SEB firmware) to fully
  eliminate the dual-writer class.

## Recommended next actions (ordered)

1. Re-enable SYS external WDT (`g_wdt.tick()`) and add MTR IWDG (hardware gate for
   freeze scenarios) — highest physical risk.
2. Add RT's positive `0x011` acquisition gate before AUTO motion (RC5, #14).
3. Add MTR REARM timeout diagnostic (RC4, #13 Case B).
4. Extend `0x011` to carry the stop reason bitmap and gate resets on it (RC1,
   #1/#15/#18).
5. Document/test SEB command-loss behavior; then complete option-3 brake IDs.
