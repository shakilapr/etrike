import sys
import os
import json
import glob
import time
import pytest
from unittest.mock import patch

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from canalyst_manager import CANalystManager
from virtual_bus import VirtualCANalystBus

def get_trace_files():
    # Find all traces in simulation/traces and native-test/traces
    sim_traces = glob.glob(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../../simulation/traces/*.jsonl')))
    native_traces = glob.glob(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../../native-test/traces/*.jsonl')))
    return sim_traces + native_traces

@pytest.mark.parametrize("trace_file", get_trace_files())
def test_replay_trace(trace_file):
    # Initialize mock manager
    manager = CANalystManager(channels=[0, 1], bitrate=500000)
    with patch('canalyst_manager.EnhancedCANalystIIBus', VirtualCANalystBus):
        manager.start()
        time.sleep(0.05) # let reader thread start
        
        try:
            with open(trace_file, 'r') as f:
                lines = f.readlines()
                
            print(f"Replaying {len(lines)} frames from {os.path.basename(trace_file)}")
            
            for line in lines:
                if not line.strip(): continue
                record = json.loads(line)
                
                # TraceRecord format: time_ms, bus, id, dlc, data, decoded
                bus_name = record["bus"]
                channel = 0 if bus_name == "high" else 1
                
                can_id_str = record["id"]
                can_id = int(can_id_str, 16)
                
                dlc = record["dlc"]
                data_hex = record["data"].split()
                data_bytes = bytes([int(b, 16) for b in data_hex])
                
                # We inject directly into the mock device buffer and let the reader thread pick it up
                # Or for strict deterministic synchronous testing, we can use bus.inject_mock_message
                manager.bus.inject_mock_message(channel=channel, can_id=can_id, data=data_bytes, dlc=dlc)
                
                # Optionally wait a tiny bit to ensure processing
                # time.sleep(0.001)

            # Wait for reader thread to drain queue
            time.sleep(0.5)
            
            batch = manager.get_ui_batch()
            
            # Assertions
            # We want to assert that the final state matches what the trace expected.
            # But the trace only has expected states per frame.
            # We can verify that error_frame_count hasn't wildly increased if not an error scenario
            if "error" not in trace_file and "corrupt" not in trace_file:
                assert batch["system_error"] is None
                
            # If the trace contained expected decodings (like the TS traces do),
            # we can verify the final state holds the last seen decoded values.
            # Let's extract the last expected decoded values for each ID from the trace.
            expected_final_states = {}
            for line in lines:
                if not line.strip(): continue
                record = json.loads(line)
                if "decoded" in record and record["decoded"]:
                    ch = 0 if record["bus"] == "high" else 1
                    cid = int(record["id"], 16)
                    expected_final_states[(ch, cid)] = record["decoded"]

            # Compare Python decoded state with Trace expected state
            for (ch, cid), expected_decoded in expected_final_states.items():
                hex_id = hex(cid)
                if hex_id in batch["channels"][ch]:
                    actual = batch["channels"][ch][hex_id]
                    if actual["decode_status"] == "ok":
                        # Compare signals
                        for key, expected_val in expected_decoded.items():
                            if key in actual["signals"]:
                                actual_val = actual["signals"][key]
                                if isinstance(expected_val, dict) and all(k.isdigit() for k in expected_val.keys()):
                                    # Convert JS serialized Uint8Array object to bytes
                                    expected_val = bytes([expected_val[str(i)] for i in range(len(expected_val))])

                                if isinstance(expected_val, (int, float)) and isinstance(actual_val, (int, float)):
                                    assert abs(expected_val - actual_val) < 1.0, f"Mismatch on {hex_id} {key}: expected {expected_val}, got {actual_val}"
                                else:
                                    assert expected_val == actual_val, f"Mismatch on {hex_id} {key}: expected {expected_val}, got {actual_val}"

        finally:
            manager.stop()
