"""ECU liveness / topology tracker (workplan §1.5, frontend §4.5).

Derives node liveness (Live/Late/Offline/Simulated/Unknown/Fault) from heartbeat
and expected-message metadata. RT 0x7FD High and Low are keyed independently and
never deduplicated. Implemented alongside freshness in §1.5.
"""

from __future__ import annotations
