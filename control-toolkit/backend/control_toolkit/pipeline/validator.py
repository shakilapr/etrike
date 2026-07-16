"""Integrity / corruption checks on decoded observations.

DLC and primary codec validation already happen inside the generated/custom
codecs. This module classifies codec status into structured validation outcomes
for the latest-value store and UI.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ValidationResult:
    """Structured validation outcome for one frame observation."""

    status: str
    ok: bool
    rules_failed: tuple[str, ...] = ()

    @property
    def is_valid(self) -> bool:
        return self.ok


# Map codec status strings to failed-rule tags for UI/diagnostics.
_STATUS_RULES: dict[str, tuple[str, ...]] = {
    "ok": (),
    "unknown_id": ("membership",),
    "wrong_message_id": ("membership",),
    "wrong_frame_format": ("frame_format",),
    "unexpected_length": ("dlc",),
    "value_out_of_range": ("range",),
    "invalid_enum": ("enum",),
    "checksum_mismatch": ("checksum",),
    "constant_mismatch": ("constant",),
    "unsupported_semantics": ("semantics",),
}


def validate_codec_status(status: str) -> ValidationResult:
    """Turn a codec status string into a structured ValidationResult."""
    if status == "ok":
        return ValidationResult(status=status, ok=True, rules_failed=())
    rules = _STATUS_RULES.get(status, ("codec",))
    return ValidationResult(status=status, ok=False, rules_failed=rules)
