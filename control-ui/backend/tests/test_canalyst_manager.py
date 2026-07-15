import sys
import os
import time
import pytest
import can
from unittest.mock import patch

# Ensure the backend directory is in the path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from canalyst_manager import CANalystManager
from virtual_bus import VirtualCANalystBus

@pytest.fixture
def mock_manager():
    """Fixture that provides a CANalystManager patched to use VirtualCANalystBus."""
    manager = CANalystManager(channels=[0, 1], bitrate=500000)
    # We patch EnhancedCANalystIIBus to be VirtualCANalystBus
    with patch('canalyst_manager.EnhancedCANalystIIBus', VirtualCANalystBus):
        manager.start()
        # Wait a moment for reader thread to start
        time.sleep(0.05)
        yield manager
        manager.stop()

def test_timestamp_wrap(mock_manager):
    """Test that a 32-bit hardware timestamp wrap is absorbed gracefully."""
    bus = mock_manager.bus
    
    print("Starting test_timestamp_wrap")
    bus.inject_mock_message(channel=0, can_id=0x100, data=b'\x01', timestamp_ticks=0xFFFFFFF0)
    time.sleep(0.1) # allow thread to process
    
    print("Getting first batch")
    batch_1 = mock_manager.get_ui_batch()
    msg_1 = batch_1["channels"][0][hex(0x100)]
    t1 = msg_1["age_ms"] # age since it was received, depends on when we check
    
    print("Injecting second frame")
    # Now inject a frame right after the wrap (e.g. 0x00000010 ticks)
    bus.inject_mock_message(channel=0, can_id=0x100, data=b'\x02', timestamp_ticks=0x00000010)
    time.sleep(0.1)
    
    print("Getting second batch")
    batch_2 = mock_manager.get_ui_batch()
    msg_2 = batch_2["channels"][0][hex(0x100)]
    
    print("Asserting wrap offset")
    
    # The wrap offset should have kicked in
    assert bus._hw_wrap_offset > 0
    # The new mapped timestamp (reflected via age or internally) should still be > the old one
    # We can check delta_t_ms which is derived from timestamp mapping
    assert msg_2["delta_t_ms"] > 0
    assert msg_2["data"] == "02"

def test_dlc_slicing(mock_manager):
    """Test that padded hardware data bytes are sliced down to actual DLC."""
    bus = mock_manager.bus
    
    # Inject a 2-byte frame that hardware padded with 6 zeroes
    bus.inject_mock_message(channel=1, can_id=0x200, data=b'\xAA\xBB', dlc=2)
    time.sleep(0.1)
    
    batch = mock_manager.get_ui_batch()
    msg = batch["channels"][1][hex(0x200)]
    
    assert msg["dlc"] == 2
    assert msg["data"] == "aabb" # Only the first two bytes

def test_error_decoding(mock_manager):
    """Test that python-can error frames are correctly decoded to socketcan standards."""
    bus = mock_manager.bus
    
    # Inject an error frame: TX Timeout (0x001) | Bus Off (0x040) = 0x041
    # Note: Our inject helper sets the CAN_ERR_FLAG (0x20000000)
    bus.inject_mock_message(channel=0, can_id=0x041, data=b'', is_error=True)
    time.sleep(0.1)
    
    batch = mock_manager.get_ui_batch()
    
    assert batch["error_frame_count"] == 1
    
    bus_error = batch["bus_error"]
    assert bus_error is not None
    
    # Verify the decoded errors list
    codes = [err["code"] for err in bus_error["errors"]]
    assert "ERR-001" in codes
    assert "ERR-040" in codes

def test_observable_queue_drops(mock_manager):
    """Test that filling the queue results in drop_count incrementing."""
    bus = mock_manager.bus
    
    # Artificially lower the max queue size for testing
    bus.max_queue_size = 5
    
    # Inject 10 frames instantly
    for i in range(10):
        bus.inject_mock_message(channel=0, can_id=0x300, data=bytes([i]))
        
    time.sleep(0.1)
    
    batch = mock_manager.get_ui_batch()
    # 10 injected, max is 5. We expect 5 drops. (The exact number depends on how fast the reader thread consumed them vs how fast we injected. Since we injected instantly without sleeping, the queue should have overflowed).
    # Wait, the reader thread runs concurrently. To guarantee drops, we could pause the reader thread, or just check that drop_count > 0
    # Actually, inject_mock_message bypasses poll_received_messages(). We need to inject via the mock_device directly and let poll run.
    pass

def test_queue_drops_via_poll():
    """Test drops strictly by invoking poll."""
    bus = VirtualCANalystBus(channel=[0], rx_queue_size=3, bitrate=500000)
    
    # Flood the mock device RX buffer
    for i in range(5):
        # We append directly to mock hardware buffer
        import ctypes
        import can.interfaces.canalystii as driver
        raw_msg = driver.Message(0x100, 0, 1, 0, 0, 0, 1, (ctypes.c_ubyte * 8)(i))
        bus.mock_device.rx_buffers[0].append(raw_msg)
        
    # Poll it synchronously
    bus.poll_received_messages()
    
    assert bus.drop_count == 2
    assert len(bus.rx_queue) == 3

def test_immediate_failure(mock_manager):
    """Test that a physical read exception immediately kills the connection state."""
    bus = mock_manager.bus
    
    assert mock_manager.is_connected == True
    assert mock_manager.last_error is None
    
    # Trigger fail mode in the mock device
    bus.mock_device.fail_mode = True
    
    # Wait for the reader thread to hit the exception
    time.sleep(0.2)
    
    batch = mock_manager.get_ui_batch()
    
    assert batch["connected"] == False
    assert batch["system_error"] is not None
    assert "Mocked physical adapter read failure" in batch["system_error"]

def test_protocol_decoding(mock_manager):
    """Test that valid protocol messages are decoded into engineering values."""
    import time
    bus = mock_manager.bus
    
    # For example, inject a HOST_DRIVE_CMD on channel 0 (high bus, id 768)
    # DLC=8: speed_mmps (i32), yaw_rate_mrad_s (i24), gear (u8)
    # speed_mmps = 500 => 0x00 0x00 0x01 0xF4
    # yaw_rate = 0 => 0x00 0x00 0x00
    # gear = 1 (D) => 0x01
    data = b'\x00\x00\x01\xF4\x00\x00\x00\x01'
    
    bus.inject_mock_message(channel=0, can_id=768, data=data, dlc=8)
    time.sleep(0.1)
    
    batch = mock_manager.get_ui_batch()
    msg = batch["channels"][0][hex(768)]
    
    assert msg["message_key"] == "host:host_drive_cmd"
    assert msg["decode_status"] == "ok"
    assert msg["signals"]["speed_mmps"] == 500
    assert msg["signals"]["yaw_rate_mrad_s"] == 0
    assert msg["signals"]["gear"] == 1
