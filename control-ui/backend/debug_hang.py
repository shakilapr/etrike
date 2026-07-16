import time
from unittest.mock import patch
from canalyst_manager import CANalystManager
from virtual_bus import VirtualCANalystBus

print("Creating manager")
manager = CANalystManager(channels=[0, 1], bitrate=500000)

with patch('canalyst_manager.EnhancedCANalystIIBus', VirtualCANalystBus):
    print("Starting manager")
    manager.start()
    
    time.sleep(0.1)
    
    print("Injecting frame")
    manager.bus.inject_mock_message(channel=0, can_id=0x100, data=b'\x01')
    time.sleep(0.1)
    
    print("Getting batch")
    batch = manager.get_ui_batch()
    print("Batch channels:", batch.get("channels", {}).get(0, {}).keys())
    
    print("Stopping manager")
    manager.stop()
    print("Done")
