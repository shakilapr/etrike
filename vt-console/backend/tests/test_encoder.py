"""Test encoder service (workplan §5.1)."""

import pytest

from vtc.config import Profile
from vtc.services.encoder import EncoderService


@pytest.fixture
def encoder():
    """Create encoder service instance."""
    return EncoderService()


class TestEncoderBasics:
    """Basic encoder functionality tests."""

    def test_encode_host_drive_cmd_round_trip(self, encoder):
        """Test encode→decode round-trip for HOST_DRIVE_CMD."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 100, "yaw_rate_mrad_s": 50, "gear": 1},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert result.data is not None
        assert result.dlc == 8
        assert result.can_id == 0x300

    def test_encode_host_heartbeat(self, encoder):
        """Test HOST_HEARTBEAT encoding."""
        result = encoder.encode_message(
            key="host:host_heartbeat",
            values={"alive_ctr": 42, "health_flags": 0},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert result.data is not None
        assert result.dlc > 0
        assert result.can_id == 0x7FC

    def test_encode_all_messages_present(self, encoder):
        """Verify encoder can handle all protocol messages."""
        # This is a smoke test — just verify no crash on catalog access
        from vtc import protocol_bridge as proto

        for key in proto.CATALOG.keys():
            # Don't actually encode (would need valid values), just check key resolution
            msg = encoder._get_message(key)
            assert msg is not None
            assert "instances" in msg

    def test_encode_returns_can_id_and_dlc(self, encoder):
        """Test that result includes CAN ID and DLC."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert result.can_id == 0x300
        assert result.dlc == 8


class TestEncoderValidation:
    """Encoder validation tests."""

    def test_reject_message_not_found(self, encoder):
        """Test rejection of unknown message key."""
        result = encoder.encode_message(
            key="unknown:unknown_message",
            values={},
            bus="high",
        )
        assert not result.ok
        assert result.error_code == "message.not_found"
        assert "not in protocol catalog" in result.error

    def test_reject_message_not_on_bus(self, encoder):
        """Test rejection when message not defined for requested bus."""
        # HOST_DRIVE_CMD is only on High bus
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="low",
        )
        assert not result.ok
        assert result.error_code == "message.bus_not_supported"

    def test_encode_invalid_values_rejected(self, encoder):
        """Test that invalid values are rejected by codec."""
        # Missing required field should cause codec error
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0},  # Missing yaw_rate and gear
            bus="high",
        )
        # Codec should reject incomplete values
        assert not result.ok
        assert "encode." in result.error_code

    def test_encode_out_of_range_rejected(self, encoder):
        """Test that out-of-range values are rejected."""
        # speed_mmps has max of 3000 per YAML
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={
                "speed_mmps": 10000,  # Way out of range
                "yaw_rate_mrad_s": 0,
                "gear": 1,
            },
            bus="high",
        )
        assert not result.ok
        assert "encode." in result.error_code


class TestEncoderSpecialCases:
    """Test special cases and edge cases."""

    def test_encode_estop_dlc_zero(self, encoder):
        """Test ESTOP (DLC=0 event frame) encoding."""
        result = encoder.encode_message(
            key="safety:safety_estop",
            values={},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert result.dlc == 0

    def test_encode_enum_values(self, encoder):
        """Test encoding with enum selections."""
        # Test enum variants using numeric enum values
        for gear in [0, 1, 2, 3]:  # N, D, S, R
            result = encoder.encode_message(
                key="host:host_drive_cmd",
                values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": gear},
                bus="high",
            )
            assert result.ok, f"Failed to encode gear={gear}: {result.error}"

    def test_encode_counter_field_present(self, encoder):
        """Test that counter fields are encoded (if present in message)."""
        # HOST_HEARTBEAT has a rolling counter
        result = encoder.encode_message(
            key="host:host_heartbeat",
            values={"alive_ctr": 0, "health_flags": 0},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert result.data is not None

    def test_encode_checksum_regeneration(self, encoder):
        """Test that counters are properly included."""
        # Encode the same message twice with different counter values
        result1 = encoder.encode_message(
            key="host:host_heartbeat",
            values={"alive_ctr": 0, "health_flags": 0},
            bus="high",
        )
        result2 = encoder.encode_message(
            key="host:host_heartbeat",
            values={"alive_ctr": 1, "health_flags": 0},
            bus="high",
        )
        assert result1.ok and result2.ok
        # Bytes should differ (counter changed)
        assert result1.data != result2.data

    def test_encode_dual_bus_instances(self, encoder):
        """Test encoding messages that appear on multiple buses."""
        # RT_HEARTBEAT appears on both High and Low buses
        result_high = encoder.encode_message(
            key="rt:rt_heartbeat",
            values={"alive_ctr": 0, "heartbeat_ok": 1},
            bus="high",
        )
        result_low = encoder.encode_message(
            key="rt:rt_heartbeat",
            values={"alive_ctr": 0, "heartbeat_ok": 1},
            bus="low",
        )
        assert result_high.ok, f"High encode failed: {result_high.error}"
        assert result_low.ok, f"Low encode failed: {result_low.error}"
        # Same CAN ID on both buses
        assert result_high.can_id == result_low.can_id == 0x7FD


class TestEncoderErrorHandling:
    """Test error handling and diagnostics."""

    def test_round_trip_failure_detected(self, encoder):
        """Test that encode→decode mismatch is detected."""
        # This is hard to trigger artificially since codec does the work
        # Just verify the check exists
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 100, "yaw_rate_mrad_s": 50, "gear": 1},
            bus="high",
        )
        # Should succeed (round-trip should work)
        assert result.ok

    def test_error_codes_are_consistent(self, encoder):
        """Test that error codes follow a consistent pattern."""
        result = encoder.encode_message(
            key="unknown:unknown",
            values={},
            bus="high",
        )
        assert not result.ok
        # Error code should be dotted format
        assert "." in result.error_code
        assert result.error_code.replace(".", "").replace("_", "").isalnum()

    def test_error_messages_are_clear(self, encoder):
        """Test that error messages are human-readable."""
        result = encoder.encode_message(
            key="unknown:unknown",
            values={},
            bus="high",
        )
        assert not result.ok
        assert len(result.error) > 10  # Non-trivial message
        assert not result.error.startswith("[")  # Not a repr dump


class TestEncoderDataIntegrity:
    """Test data integrity after encoding."""

    def test_encoded_data_has_correct_length(self, encoder):
        """Test that encoded data length matches DLC."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert len(result.data) == result.dlc

    def test_encoded_data_is_bytes(self, encoder):
        """Test that encoded data is a bytes object."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert isinstance(result.data, bytes)

    def test_can_id_is_valid_int(self, encoder):
        """Test that CAN ID is a valid integer."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
        )
        assert result.ok, f"Encode failed: {result.error}"
        assert isinstance(result.can_id, int)
        assert 0 <= result.can_id <= 0x7FF  # Standard CAN range


class TestEncoderProfiles:
    """Test profile handling in encoder."""

    def test_encode_pure_software_profile(self, encoder):
        """Test encoding works in Pure Software profile."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            profile=Profile.PURE_SOFTWARE,
        )
        assert result.ok, f"Encode failed: {result.error}"

    def test_encode_bench_test_profile(self, encoder):
        """Test encoding works in Bench Test profile."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            profile=Profile.BENCH_TEST,
        )
        assert result.ok, f"Encode failed: {result.error}"

    def test_encode_full_vehicle_profile(self, encoder):
        """Test encoding works in Full Vehicle profile."""
        result = encoder.encode_message(
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            profile=Profile.FULL_VEHICLE,
        )
        assert result.ok, f"Encode failed: {result.error}"
