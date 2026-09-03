# RMT_14 — Architecture & Code Analysis

An ESP-IDF FreeRTOS application for an e-trike **drive-by-wire / brake-by-wire** gateway.

It reads **6 RC/PWM receiver channels** using the legacy RMT driver, scales each pulse into
engineering values, pushes a synchronized snapshot onto a FreeRTOS queue, and converts that
snapshot into **TWAI/CAN 2.0** frames that describe ignition, gear selector, brake and the
steering/pedal signals of the trike.

Target silicon: **ESP32** (classic, dual-core, APB = 80 MHz). See `sdkconfig`:
`CONFIG_IDF_TARGET="esp32"`, `CONFIG_FREERTOS_HZ=100`.

> This document describes the code **as it is actually written**, including every wiring pin,
> threshold, scaling formula, frame layout, task wiring, and a section at the end listing
> functional quirks / likely bugs found in the source.

---

## 1. Project Layout

```
RMT_14/
├── CMakeLists.txt                 # Root build file (project RMT_14)
├── sdkconfig                      # esp32, 2 MB flash
├── main/
│   ├── CMakeLists.txt             # links main -> RMT, QUEUE, CAN (private), freertos
│   └── main.c                     # app_main: init, queue, 3 tasks
└── components/
    ├── RMT/
    │   ├── CMakeLists.txt         # rmt_task.c ; priv: QUEUE ; req: driver
    │   ├── rmt_task.h             # API + NUM_CHANNELS + extern receiverHandler
    │   └── rmt_task.c             # PWM capture, scaling, queue send
    ├── QUEUE/
    │   ├── CMakeLists.txt
    │   ├── queue_setup.h          # rmt_Data_t struct + queue decl
    │   └── queue_setup.c          # creates rmt_Queue (global)
    └── CAN/
        ├── CMakeLists.txt         # can.c, CAN_manager.c, can_rest.c ; priv: QUEUE
        ├── can.h                  # task prototypes + extern ret
        ├── can.c                  # can_task: RMT data -> CAN frames
        ├── CAN_manager.h/.c       # CAN_init(): TWAI install/start
        └── can_rest.c             # can_reset_task: periodic BUS-OFF watchdog
```

Component dependency graph (from the CMake files):

```
main ──PRIV_REQUIRES──> RMT ──PRIV_REQUIRES──> QUEUE
   │                      │
   ├──PRIV_REQUIRES──> QUEUE (standalone, no deps)
   │
   └──PRIV_REQUIRES──> CAN ──PRIV_REQUIRES──> QUEUE
                          │
                          └──REQUIRES──> driver  (twai, gpio, rmt)
```

---

## 2. Hardware Wiring (GPIO Map)

### 2.1 RMT inputs — 6 PWM channels from the RC receiver

| RMT channel | GPIO  | Signal use (by consumer decode)  |
|-------------|-------|----------------------------------|
| `RMT_CHANNEL_0` | GPIO 18 | **Drive-by-wire** value → CAN 0x169 |
| `RMT_CHANNEL_1` | GPIO 19 | **Brake-by-wire** value → CAN 0x7B9 |
| `RMT_CHANNEL_2` | GPIO 14 | Third analog value → CAN 0x0AA |
| `RMT_CHANNEL_3` | GPIO 32 | Pass-through value → CAN 0x112 |
| `RMT_CHANNEL_4` | GPIO 13 | **Ignition** digital decode |
| `RMT_CHANNEL_5` | GPIO 4  | **Gear selector** (P/R/D) decode |

Defined in `components/RMT/rmt_task.c` (`pwm_pins[]`, `rmt_channels[]`).

### 2.2 TWAI / CAN bus

| Function | GPIO | Config value |
|----------|------|--------------|
| CAN TX | GPIO 21 | `twai_general_config_t.mode` = `TWAI_MODE_NORMAL` |
| CAN RX | GPIO 22 | `twai_timing_config_t` = `TWAI_TIMING_CONFIG_500KBITS()` |
| —      | —    | Filter = `TWAI_FILTER_CONFIG_ACCEPT_ALL()` (no RX filtering) |

Configured in `CAN_manager.c` `CAN_init()` using the legacy `driver/twai.h` API.

### 2.3 RMT timing configuration

Per channel (`setupRMT()`):

```c
.clk_div = 40,                      // APB source / 40 -> counter tick = 0.5 us
.rx_config.filter_en = true,
.filter_ticks_thresh = 500,         // RX glitch filter threshold
.idle_threshold = 20000,            // end-of-frame idle timeout
mem_block_num = 1,
```

- Ring buffer per channel: **2048 bytes**, allocated by `rmt_driver_install(ch, 2048, 0)`.
- **Counter tick** = APB 80 MHz ÷ `clk_div` 40 = 2 MHz → **1 RMT counter tick = 0.5 µs**.
  The in-code comment `// 1 tick = 1us (80MHz/80)` disagrees with its own config value
  (`clk_div = 40`, and the divisor it quotes, 80, would give 1 µs). Consequently every captured
  `duration0` — and hence every `high_us` value, which is named and treated as microseconds — is
  really a raw count of **0.5 µs ticks**: e.g. a 1500 µs RC pulse reads ~3000, not 1500. All the
  decode thresholds (ch1 `>= 630`, ch4 `2100`, ch5 `2200/2500/2600`) and the scaling constants in
  §5.2 are compared/applied against these raw tick counts, so signal meaning must be confirmed on
  hardware.
- **Glitch filter** (`filter_ticks_thresh = 500`): the legacy RMT `rx_config.filter_ticks_thresh`
  field and the driver's `rmt_set_rx_filter()` threshold are **`uint8_t`**, so the configured
  value `500` wraps to `500 − 256 = 244`. The threshold is also in ticks, not µs, so the in-code
  comment `// 100us noise filter` matches neither the value nor the unit actually programmed.
- **Idle timeout** (`idle_threshold = 20000`): the legacy `rmt_set_rx_idle_thresh()` counts in
  **channel counter-clock ticks** → 20000 × 0.5 µs = **10 ms**. The in-code comment
  `// 20ms idle timeout` is 2× off (it would only hold at the 1 µs/tick setting).

---

## 3. Shared Data & Synchronization (QUEUE component)

### 3.1 The item type

```c
typedef struct {
    uint8_t  channel_num;   // 0..5
    uint32_t high_us;       // measured / scaled pulse value (ticks)
} rmt_Data_t;              // sizeof = 8 (1 + pad + 4)
```

### 3.2 The queue

```c
QueueHandle_t rmt_Queue = NULL;               // global, defined in queue_setup.c
rmt_Queue = xQueueCreate(5, sizeof(rmt_Data_t[6]));
```

- **Length 5**, each item is one **6-element array** (`rmt_Data_t[6]` = 48 bytes) = one
  full synchronized snapshot of all six channels.
- `setup_rmt_queue()` is called from `app_main()` before any task starts.
- The queue is shared between the **RMT producer task** and the **CAN consumer task**;
  access is through the standard FreeRTOS queue API (no separate mutex needed).

---

## 4. Startup & Task Wiring (`main/main.c`)

```c
TaskHandle_t receiverHandler = NULL;          // global, owned by main

app_main:
    ESP_ERROR_CHECK(CAN_init());              // TWAI install + start
    setup_rmt_queue();                        // create queue
    xTaskCreate(can_task,        "rmt_task",       4096, NULL, 10, &receiverHandler);
    xTaskCreate(can_reset_task,  "CAN_rest_task",  4096, NULL,  9, NULL);
    xTaskCreate(rmt_task,        "rmt_task",       4096, NULL,  8, NULL);
```

### 4.1 Task table

| Task function | Stack | Priority | Handle | Role |
|---------------|-------|----------|--------|------|
| `can_task`        | 4096 B | 10 | `receiverHandler` | Consumer: notified on each snapshot, reads queue, transmits CAN |
| `can_reset_task`  | 4096 B |  9 | — | BUS-OFF watchdog, poll every 600 ms |
| `rmt_task`        | 4096 B |  8 | — | Producer: captures RMT, scales, sends to queue + notifies |

Notes:

- The task *name strings* are misleading: `can_task` is registered as `"rmt_task"` and the real
  RMT producer is also named `"rmt_task"` — both show the same name in the scheduler.
- `receiverHandler` is **defined in main.c** and **declared `extern` in `rmt_task.h`**. It is
  filled when `can_task` is created, and the RMT task uses it to fire the notification.
- The scheduler is already running when `app_main` executes, so `can_task` (priority 10) can
  preempt the still-constructing `app_main`; the queue exists before any task is created so
  there is no NULL-queue hazard.

### 4.2 Producer → Consumer synchronization handshake

1. `rmt_task` builds a full `rmt_Data_t[6]` snapshot and calls `xQueueSend(rmt_Queue, …, 0)`.
2. **Only if the send succeeded** (`pdTRUE`), it calls
   `xTaskNotify(receiverHandler, 0, eNoAction)` to wake `can_task`.
3. `can_task` blocks in `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` until woken, then does a
   blocking `xQueueReceive(rmt_Queue, &received_array, portMAX_DELAY)`.

Because the notify is only sent *after* a successful queue push, the consumer should always
find an item available. The notify value is unused (`0`, `eNoAction`); `pdTRUE` clears the
notification count on exit. A full queue silently drops the oldest *producer* attempt
(non-blocking send), i.e. samples are dropped before the notify ever fires.

---

## 5. RMT Component (`rmt_task.c`)

### 5.1 Producer loop (per 200 ms tick)

`rmt_task()` first calls `setupRMT()` (configures + starts all six RX channels), then loops:

For each channel `ch` in 0..5:

1. Drain the ring buffer, **keeping only the newest sample** and returning all older items:
   ```c
   while ((item = xRingbufferReceive(rb[ch], &length, 0)))
       { return previous; keep latest; }
   ```
2. If a sample exists, `high_us[ch] = latest_item->duration0` (only the **high/level-0**
   duration is used; low time / period / duty are commented out). Otherwise `high_us[ch] = 0`.
3. Comparison against the previous stored value (see quirk §9.1 — as written this is **always
   true**, so the stored-value refresh branch below is dead code):
   ```c
   if ((high_us[ch] - past_val[ch].high_us) <= 30
    || (high_us[ch] - past_val[ch].high_us) >= -30)
   ```
   - **Taken branch:** recompute `send_val` from the *stored* `past_val[ch].high_us`
     (does **not** refresh `past_val`).
   - **Else branch (dead code, see §9.1):** first refresh `past_val[ch].high_us = high_us[ch]`,
     then recompute `send_val` from the new value.
4. Fill `send_array[ch] = { ch, send_val }`.

### 5.2 Per-channel scaling (`send_val` computation)

| ch | Signal (CAN id) | If stored == 0 | Else formula | Clamp |
|----|-----------------|----------------|--------------|-------|
| 0 | Drive-by-wire (0x169) | `30000` | `(uint32_t)(0.401 * stored + 28770.25)` | — |
| 1 | Brake-by-wire (0x7B9) | `600` | `(uint32_t)(0.5025 * stored − 941.8)` | — |
| 2 | (0x0AA) | `4960` | `(uint32_t)(stored * 9.85 − 14897)` | `19000` if result > `18800` |
| 3 | (0x112) | — | `= stored` (pass-through) | — |
| 4 | ignition | — | `= stored` (pass-through) | — |
| 5 | gear | — | `= stored` (pass-through) | — |

Several alternative calibrations are present but commented out
(e.g. `0.902*x + 27346.12`, `0.369*x + 28871.3`, `22.487*x − 35656.84`, `4.51278*x + 650.5`).
Case 2's `if (stored > 9000 || stored == 0)` picks the `4960` default for out-of-range inputs.

> Case 0's output (~29 000–30 000) and the `(x >> 8)` / `& 0xFF` packing in the CAN layer
> (§6.5) indicate these scaled values are intended to be consumed as 16-bit quantities
> (e.g. servo/steering angle counts) split into two CAN payload bytes.

### 5.3 Publish step

```c
for (i in 0..5) ESP_LOGI(TAG, "CH%d GPIO%2d: High=%lu", ...);   // debug print each cycle
if (xQueueSend(rmt_Queue, &send_array, 0) == pdTRUE)
    if (receiverHandler != NULL)
        xTaskNotify(receiverHandler, 0, eNoAction);
vTaskDelay(pdMS_TO_TICKS(200));   // fixed 200 ms publication period
```

---

## 6. CAN Component

### 6.1 Initialization (`CAN_manager.c`)

`CAN_init()` performs `twai_driver_install()` then `twai_start()`. Failure paths log the error
and return it (install failure uninstalls nothing; start failure calls `twai_driver_uninstall()`).
A large block of commented-out duplicate start code remains in the file. Reaching this point
after init fails is guarded by `ESP_ERROR_CHECK` in `app_main` (reboot).

### 6.2 Consumer task state machine (`can.c` `can_task`)

State latched per cycle (declared once, outside the loop, so values persist between snapshots):

| Variable | Set by channel | Meaning |
|----------|----------------|---------|
| `ignition_on` | ch4 | ignition switch state |
| `parking` / `drive` / `reverse` | ch5 | gear selector (mutually overwrite) |
| `brake_pressed` | ch1 | brake switch / threshold |
| `channel_id` | ch0–3 | current analog source CAN id (see §6.3) |
| `channel_id_2` | — | CAN id of the digital status frame (`0x0BB`) |
| `digital_tansmit` | — | flag that a digital frame must be sent |

**Wake → receive → decode loop:**

```c
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
xQueueReceive(rmt_Queue, &received_array, portMAX_DELAY);
for (i = 0; i < 6; i++) {
    log channel/high value;
    switch (received_array[i].channel_num) { ... decode ... }
    build msg2 (0x0BB digital)        // per channel iteration
    build msg1 (analog) + transmit    // per channel iteration
}
```

Note the decode and transmit all happen **inside the 6-channel loop**, so the gear / brake /
ignition bits may not be final while the first channels are already being transmitted
(ch0/drive is processed before ch5/gear has been decoded in the same snapshot — see §9.4).

### 6.3 Channel → CAN-id and decode logic

| ch | CAN id (`channel_id`) | Decode rule | Latched output |
|----|-----------------------|-------------|----------------|
| 0 | `0x169` | — | steering/drive analog value |
| 1 | `0x7B9` | `high_us >= 630` → brake pressed | `brake_pressed` = 1/0 |
| 2 | `0x0AA` | — | analog value |
| 3 | `0x112` | — | analog value |
| 4 | *(unchanged)* | `high_us <= 2100` → off; `high_us > 2100` → on | `ignition_on` |
| 5 | *(unchanged)* | see gear bands below | `parking`/`drive`/`reverse` |
| default | `0x000` | prints `invalid channel id!!` | — |

Gear bands (ch5, evaluated in order, overlaps possible — see §9.3):

| Condition (high_us) | Result |
|---------------------|--------|
| `> 2200 && < 2600` | `parking = 1; drive = 0; reverse = 0;` |
| `else if < 2150 && > 10` | `reverse = 1; parking = 0; drive = 0;` |
| `else if > 2500` | `drive = 1; reverse = 0; parking = 0;` |
| else | nothing (previous gear state is retained) |

> ch4 and ch5 do **not** assign `channel_id`, so `channel_id` keeps the value from the
> previously processed channel (normally `0x112` from ch3) — see §9.2.

### 6.4 Digital status frame — msg2 (CAN id `0x0BB`)

`msg2` is a standard 8-byte data frame (`.extd=0, .rtr=0, .ss=0, .self=0, .dlc_non_comp=0,
.data_length_code=8`) built **once per loop iteration** (i.e. up to 6× per snapshot). It uses a
partial designated initializer, so C zero-fills all unlisted members — including `data[8]` —
and every active path additionally writes `data[0..7]` explicitly (`data[0]` = state code,
`data[1..7]` = 0). Only `data[0]` ever carries information.

Gating logic:

```c
if (ignition_on) {
    if (ignition_on && parking)                 msg2.data[0] = 0x03, ch2 = 0x0BB, digital_tansmit = 1;
    if (drive && ignition_on)                   msg2.data[0] = 0x05, ...;
    if (reverse && ignition_on)                 msg2.data[0] = 0x09, ...;
    if (brake_pressed && ignition_on)           msg2.data[0] = 0x11, ...;
    if (brake_pressed && ignition_on && parking) msg2.data[0] = 0x13, ...;
    if (brake_pressed && ignition_on && drive)   msg2.data[0] = 0x15, ...;
    if (brake_pressed && ignition_on && reverse) msg2.data[0] = 0x19, ...;
} else {                                        // ignition off
    channel_id_2 = 0x0BB; msg2.data[0] = 0x00; digital_tansmit = 1;
}
```

Because these are sequential independent `if`s on the same `data[0]`, the **last matching**
condition wins (most-specific combos are listed last, which gives correct precedence).
The whole block was previously structured as one big `if/else if` chain and is now
commented out.

> **msg2 identifier capture-order bug.** `msg2` is constructed with
> `.identifier = channel_id_2` (line 143) — i.e. *before* any of the branches below run — and
> `msg2.identifier` is never re-written afterwards. `channel_id_2` starts at 0 (line 19) and is
> only assigned `0x0BB` *inside* the state branches. So **on the very first transmission the
> frame goes out with identifier 0**. On every subsequent loop iteration `channel_id_2` still
> holds the previous `0x0BB`, so the identifier is correct only from the second iteration on —
> and it is correct *by accident*, not by design (see §9.6).

**Digital state codes (data[0] of 0x0BB):**

| data[0] | Meaning |
|---------|---------|
| `0x00` | ignition off / nothing |
| `0x03` | parking (ignition on) |
| `0x05` | drive |
| `0x09` | reverse |
| `0x11` | brake pressed |
| `0x13` | brake + parking |
| `0x15` | brake + drive |
| `0x19` | brake + reverse |

If `digital_tansmit`, msg2 is transmitted with `twai_transmit(&msg2, pdMS_TO_TICKS(20))`
then `digital_tansmit` is cleared. Because the entire build/transmit sits inside the 6-channel
`for` loop, **up to 6 identical digital frames can be sent per snapshot** — the flag and
`data[0]` are recomputed fresh for each `i` from the (latched) ignition/gear/brake state, so
every iteration of a snapshot where a gear is engaged transmits 0x0BB again.

### 6.5 Analog value frame — msg1 (one per channel, ids 0x169/0x7B9/0x0AA/0x112)

`msg1` mirrors msg2's frame header but with `.identifier = channel_id`. Its payload is chosen
by the vehicle motion state:

**Motion state = `(ignition_on && drive) || (ignition_on && reverse)`** (i.e. vehicle
engaged/rolling):

| id | data[0] | data[1] | data[2] | data[3] | data[4] | data[5..7] |
|----|---------|---------|---------|---------|---------|-----------|
| 0x169 | `0x02` | `high_us >> 8` | `high_us & 0xFF` | `0x00` | `0x7E` | `0x00` |
| 0x7B9 | `0x02` | `high_us >> 8` | `high_us & 0xFF` | `0x00` | `0x00` | `0x00` |
| 0x0AA | `high_us >> 8` | `high_us & 0xFF` | `0x00` | … | `0x00` | `0x00` |
| 0x112 | *(no `case` — payload stays all-zero from the initializer, frame still transmitted)* | | | | | |

**Idle state (else branch — ignition off or not in drive/reverse):**

| id | data[0] | data[1] | data[2] | data[3..7] |
|----|---------|---------|---------|-----------|
| 0x169 | all `0x00` | | | |
| 0x0AA | `(4960 >> 8) & 0xFF` = `0x13` | `4960 & 0xFF` = `0x60` | `0x00` | `0x00` |
| 0x7B9 | `0x02` | `0x02` | `0x58` | `0x00` |
| 0x112 | *(no `case` — payload stays all-zero)* | | | |

`high_us` here is the **already-scaled** value produced by the RMT task (§5.2), packed big-endian
into two bytes. So the trike only broadcasts live steering (0x169) / brake (0x7B9) values while a
gear is engaged and ignition is on; at idle, 0x0AA broadcasts the constant `4960` (its scaled
"neutral"/default) and 0x7B9 broadcasts fixed bytes `02 02 58 …`.

Transmission: `twai_transmit(&msg1, pdMS_TO_TICKS(20))`, error logged as
`CAN msg %X send failed`. msg1 is sent for **every** channel iteration (up to 6 frames per
snapshot), independent of `digital_tansmit`.

### 6.6 Bus-off handling

Two independent recovery mechanisms exist:

1. **Inline (in `can_task`)** — after a failed `twai_transmit` of msg1 or msg2:
   ```c
   twai_get_status_info(&status);
   if (status.state == TWAI_STATE_BUS_OFF) {
       twai_stop(); vTaskDelay(10 ms); twai_start();  // "restarted successfully"
   }
   ```
2. **Periodic watchdog (`can_rest.c`)** — every `CAN_RECOVERY_TASK_PERIOD_MS = 600 ms`:
   ```c
   twai_get_status_info(&status);
   if (status.state == TWAI_STATE_BUS_OFF) { twai_stop(); delay 10ms; twai_start(); delay 10ms; }
   ```
   An ERROR-PASSIVE monitoring branch is present but commented out.

---

## 7. CAN Message Summary

| CAN id | Type | Direction | Content |
|--------|------|-----------|---------|
| `0x0BB` | digital state | TX | data[0] = state code (§6.4); up to 6× per snapshot (once per loop iteration) |
| `0x169` | analog | TX | drive-by-wire value (scaled, 16-bit BE) while engaged; all-zero at idle |
| `0x7B9` | analog | TX | brake-by-wire value (16-bit BE) while engaged; fixed `02 02 58 …` at idle |
| `0x0AA` | analog | TX | third analog value (16-bit BE) while engaged; fixed `4960` at idle |
| `0x112` | analog | TX | frame transmitted but **no payload case exists** → always all-zero data |

Bus: 500 kbit/s, standard frames, 8 data bytes, accept-all filter, normal mode, no RX task
(the device only transmits).

---

## 8. Timing / Flow Summary (one full cycle)

```
RMT channels 0..5 (GPIO 18/19/14/32/13/4)
      │  0.5 µs/tick RMT RX, newest-sample ring-buffer capture
      ▼
rmt_task (every 200 ms)
      │  scale per channel (§5.2) -> rmt_Data_t[6] snapshot
      ▼
xQueueSend(rmt_Queue, snapshot)  (5-deep)
      │  on success: xTaskNotify(receiverHandler, eNoAction)
      ▼
can_task  (waiting on ulTaskNotifyTake)
      │  xQueueReceive -> 6-channel decode loop (§6.2)
      ├─ msg2: 0x0BB digital status frame   (when digital_tansmit, up to 6×/snapshot)
      └─ msg1: 0x169 / 0x7B9 / 0x0AA analog frames  (every iteration; 0x112 also sent but zero-payload)
      │
      └─ on TX failure + BUS_OFF -> twai_stop/start
                                      ▲
can_reset_task (every 600 ms)  ───────┘  independent BUS-OFF watchdog
```

---

## 9. Functional Quirks & Likely Bugs (as written)

These are all present in the committed source and are worth reviewing before relying on the
behaviour described above:

1. **Dead code in the RMT change-detection.** The condition
   `(diff <= 30) || (diff >= -30)` is true for *every* integer `diff` (its complement would need
   `diff > 30 && diff < -30`), so the `else` branch — the only place `past_val[]` is refreshed
   from live `high_us` — never runs. `past_val[]` therefore stays at its initialiser (all 0),
   and the producer permanently sends the §5.2 defaults (`30000`, `600`, `4960`, `0`, `0`, `0`).
   Likely the author intended `&&` (`|diff| <= 30`), plus moving the `past_val = high_us`
   refresh into the common path.

2. **`channel_id` leaks across channels.** ch4/ch5 (and the invalid-id `default` case) don't
   assign `channel_id`, so the last analog id (`0x112`, set by ch3) is re-used for their msg1
   frames. Since each loop iteration transmits msg1, `0x112` frames are actually emitted for
   i = 3, 4 **and** 5 → up to **three** identical all-zero `0x112` frames per snapshot, and the
   `default` `0x000` assignment (which would transmit with identifier 0) can never be reached
   for a real out-of-range channel id.

3. **Gear bands overlap / gaps.** `parking` is `(2200, 2600)` and `drive` is `> 2500`, so a
   value in `(2500, 2600)` matches both — ordering makes `drive` win but the ranges should be
   disjoint. Conversely, values in `[2150, 2200]` and `>= 2600` match *no* branch and leave the
   previous gear state latched (there is no else that resets to a safe default).

4. **Ordering dependency within a snapshot.** ch0 (0x169) and ch1 (0x7B9/brake) are processed
   and transmitted before ch4 (ignition) and ch5 (gear) are decoded in the *same* snapshot, so
   the very first frames of a cycle use the previous snapshot's ignition/gear/brake state.

5. **`ret` is never updated.** `esp_err_t ret` (global, `extern` in `can.h`) is checked after
   every `twai_transmit` (`if (ret != ESP_OK) …`), but nothing assigns the transmit result to
   it, so those error branches are unreachable.

6. **`msg2.identifier` capture-order bug (see §6.4).** Because `msg2` is declared *before* the
   state branches assign `channel_id_2 = 0x0BB`, and `msg2.identifier` is never rewritten, the
   very first 0x0BB frame of the task's life is transmitted with identifier **0**. From the
   second loop iteration onward `channel_id_2` happens to already hold `0x0BB` (retained from
   the previous iteration), so later frames are correct only as a side effect.

7. **µs vs tick units (see §2.3).** The code names and comments treat RMT counts as
   microseconds and use thresholds such as `630`/`2100`/`2200` accordingly, but with
   `clk_div = 40` each counter tick is `0.5 µs`; thresholds and the §5.2 calibration constants
   only make sense in one of the two unit systems, so signal meaning must be re-verified on
   hardware.

8. **Duplicate task names.** Both `can_task` and the real RMT producer are registered with the
   string `"rmt_task"`, and `can_reset_task` as `"CAN_rest_task"` (typo), which makes debugging
   by task name ambiguous.

9. **Timing.** The producer runs on a fixed 200 ms schedule while the RC PWM frame period is
   20 ms; combined with "keep only the newest sample," most incoming frames are discarded and
   the effective control-loop rate is 5 Hz.

10. **`0x112` never carries data.** Neither the engaged nor the idle msg1 switch has a
   `case 0x112`, so the ch3 pass-through value (and the channel id leak from ch4/ch5) always
   produces a zero-payload frame — if channel 3's value is meant to be transmitted on 0x112,
   the payload cases are simply missing.

---

## 10. Build Notes

- ESP-IDF classic projects; only the `driver` component is required by RMT/CAN, `QUEUE` has no
  IDF requirements. Task stacks are 4096 B each; `CONFIG_ESP_MAIN_TASK_STACK_SIZE` default
  (3584 B).
- `sdkconfig`: target `esp32`, flash 2 MB, FreeRTOS tick 100 Hz, dual-core SMP default
  (tasks unpinned).
