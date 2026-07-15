import pytest
import asyncio
from fastapi.testclient import TestClient
from unittest.mock import patch
import json
import time
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from main import app, startup_event, shutdown_event
import main
from virtual_bus import VirtualCANalystBus

@pytest.fixture
def client():
    """Starts the FastAPI app with the mock virtual bus."""
    with patch('canalyst_manager.EnhancedCANalystIIBus', VirtualCANalystBus):
        with TestClient(app) as client:
            yield client

def test_inject_command(client):
    """Test REST POST /api/inject to encode and send a command."""
    # We want to send HOST_DRIVE_CMD (768) on 'high' bus
    payload = {
        "message_key": "host:host_drive_cmd",
        "bus": "high",
        "values": {
            "speed_mmps": 1000,
            "yaw_rate_mrad_s": 0,
            "gear": 1 # D
        }
    }
    
    resp = client.post("/api/inject", json=payload)
    assert resp.status_code == 200
    
    # Verify it hit the TX buffer of the virtual bus
    # channel 0 is high bus
    tx_buffer = main.manager.bus.mock_device.tx_buffers[0]
    assert len(tx_buffer) == 1
    
    sent_msg = tx_buffer[0]
    assert sent_msg.can_id == 768
    
    # 1000 = 0x00 0x00 0x03 0xE8
    # gear = 1 = 0x01 at byte 7
    # So expected data is roughly: b'\x00\x00\x03\xE8\x00\x00\x00\x01'
    data_bytes = bytes(sent_msg.data)[:8]
    assert data_bytes == b'\x00\x00\x03\xE8\x00\x00\x00\x01'

def test_websocket_stream(client):
    """Test WebSocket GET /api/stream to receive decoded telemetry."""
    # We need to inject a message so it appears in the stream
    data = b'\x00\x00\x01\xF4\x00\x00\x00\x01' # speed 500, gear D
    main.manager.bus.inject_mock_message(channel=0, can_id=768, data=data, dlc=8)
    time.sleep(0.1) # let the reader thread process it
    
    with client.websocket_connect("/api/stream") as websocket:
        batch = websocket.receive_json()
        assert batch["connected"] == True
        
        # Check channel 0 (high bus)
        ch_0 = batch["channels"]["0"]
        assert "0x300" in ch_0 # 768 is 0x300
        
        msg = ch_0["0x300"]
        assert msg["message_key"] == "host:host_drive_cmd"
        assert msg["signals"]["speed_mmps"] == 500
        assert msg["signals"]["gear"] == 1
