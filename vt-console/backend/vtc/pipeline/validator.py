"""Integrity / corruption checks (workplan §1.4).

DLC, range, enum, checksum, and counter validation reuse the generated codec
status plus the YAML counter/checksum metadata. Implemented in step §1.4.
"""

from __future__ import annotations
