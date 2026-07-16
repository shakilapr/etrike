"""Bounded frame history (workplan §1.5).

Fixed-size ring for the chronological raw view. Bounded and instrumented; drops
are counted and surfaced, never silent. Implemented in step §1.5.
"""

from __future__ import annotations
