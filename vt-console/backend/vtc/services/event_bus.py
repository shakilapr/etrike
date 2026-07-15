"""Internal event distribution (workplan §1.1, §9).

Fan-out of transport/diagnostic/state events to WebSocket subscribers with
per-client bounded queues and sequence/gap contracts (can-analyzer-research §6).
Implemented from Phase 1 §1.6 (stream) onward.
"""

from __future__ import annotations
