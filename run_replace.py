import os

filepath = r'c:\projects\etrike\control-toolkit\architecture-control-toolkit.md'
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Rename references
content = content.replace('vt-console', 'control-toolkit')
content = content.replace('VT-Console', 'Control Toolkit')
content = content.replace('vt_console', 'control_toolkit')

# 2. Timestamp Simplification
content = content.replace(
    'uses a 20 ms receive poll delay, stores its optional bounded RX queue in `deque(maxlen=...)` where old entries can disappear without an exposed counter, and converts a device-relative 100 μs timestamp into seconds.',
    'uses a 20 ms receive poll delay and stores its optional bounded RX queue in `deque(maxlen=...)` where old entries can disappear without an exposed counter.'
)

content = content.replace(
    'The CANalyst-II USB protocol returns frames grouped by channel. Official `python-can` documentation states that order is preserved within a channel but frames from Channel 0 and Channel 1 may be delivered out of order relative to one another; the hardware timestamp is the correct cross-channel timing evidence. Therefore the application maintains per-channel sequence plus hardware timestamp. It does not treat backend ingestion order as vehicle-wide CAN order.\n\nRequired behavior:\n\n- preserve standard/extended, data/remote, channel, DLC, and exactly `data[0:DLC]` rather than padded trailing bytes;\n- preserve the CANalyst-II device receive timestamp at its 100 μs resolution, unwrap/reset-detect it, and map it into the session timebase while retaining the raw device value;\n- retain per-channel order and assign a backend sequence for deterministic merging;\n- order each channel by its preserved order and use hardware timestamp for cross-channel analysis;',
    'The CANalyst-II USB protocol returns frames grouped by channel. Official `python-can` documentation states that order is preserved within a channel but frames from Channel 0 and Channel 1 may be delivered out of order relative to one another. For UI observation, the backend assigns a monotonic software arrival timestamp (`time.monotonic_ns()`) to order events across buses. Strict sub-millisecond hardware-level cross-channel latency analysis is deferred.\n\nRequired behavior:\n\n- preserve standard/extended, data/remote, channel, DLC, and exactly `data[0:DLC]` rather than padded trailing bytes;\n- use `time.monotonic_ns()` as the primary session-monotonic arrival time;\n- order each channel by its preserved adapter sequence, and use software arrival time for cross-channel analysis;'
)

content = content.replace(
    '1. Capture adapter epoch, device channel, device timestamp, and backend arrival time.',
    '1. Capture adapter epoch, device channel, and backend software arrival time.\n2. Ignore device-relative timestamps (unwrapping and mapping are prone to undocumented rollover behavior).'
)

content = content.replace(
    '1. Maps the device-relative timestamp into the backend session timebase.\n2. Detects device timestamp reset or wrap and starts a new mapping segment.',
    ''
)

content = content.replace(
    'The CANalyst-II device timestamp is retained as source evidence. Backend arrival uses a monotonic host clock. Mapped session time is monotonic even if the adapter timestamp wraps or resets.\n\nWithin each channel, receive order is authoritative. Across High and Low, ingestion order is not authoritative because the adapter returns channel groups separately. Cross-bus analysis uses mapped device timestamp, then channel sequence as a deterministic tie-breaker.',
    'The CANalyst-II device timestamp is ignored due to rollover unpredictability. Backend arrival uses a monotonic host clock. Mapped session time is monotonic.\n\nWithin each channel, receive order is authoritative. Across High and Low, ingestion order is used as a best-effort proxy, understanding it is subject to USB polling jitter.'
)

# 3. Websocket / Delta / Clock Offset Simplification
content = content.replace(
    'Backend and browser monotonic clocks have different origins. A ping/pong exchange estimates browser/backend clock offset, round-trip time, and uncertainty; timestamps are never directly subtracted across clocks without that mapping. The frontend records its own arrival and render times. This provides four separately visible measurements:\n\n1. **Frame age:** now minus CAN receive time.\n2. **Transport delay estimate:** mapped browser arrival minus backend publish time, reported with uncertainty.\n3. **Render delay:** visual commit minus browser arrival time.\n4. **End-to-end visual age:** now minus CAN receive time for the value currently on screen.',
    'Backend and browser monotonic clocks have different origins. Because the tool runs locally (localhost), complex ping/pong clock offset estimation is not required. The frontend trusts the backend\'s freshness ages and supplements them with its own local `performance.now()` timers. This provides separately visible measurements:\n\n- **Source age:** backend timestamp since the message was originally created.\n- **Render age:** frontend timestamp since the message arrived at the browser.'
)

content = content.replace(
    '3. establish clock-offset/RTT estimate;',
    '3. subscribe to state broadcasts;'
)

content = content.replace(
    '3. Estimate browser/backend clock offset and uncertainty.\n4. Subscribe to state broadcasts.',
    '3. Subscribe to state broadcasts.'
)

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(content)

print("Architecture file updated.")
