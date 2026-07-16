"""Codec status classification for the validation layer."""

from control_toolkit.pipeline.validator import validate_codec_status


def test_ok_is_valid() -> None:
    r = validate_codec_status("ok")
    assert r.ok
    assert r.rules_failed == ()


def test_dlc_error_maps_to_rule() -> None:
    r = validate_codec_status("unexpected_length")
    assert not r.ok
    assert "dlc" in r.rules_failed


def test_checksum_error_maps_to_rule() -> None:
    r = validate_codec_status("checksum_mismatch")
    assert not r.ok
    assert "checksum" in r.rules_failed


def test_unknown_status_gets_generic_rule() -> None:
    r = validate_codec_status("something_new")
    assert not r.ok
    assert r.rules_failed == ("codec",)
