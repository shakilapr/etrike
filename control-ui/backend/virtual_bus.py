from collections import deque
import can
from typing import List
import time

class VirtualCANalystDevice:
    """Mock for the CANalystDevice C-library interface."""
    def __init__(self, device_index=0):
        self.device_index = device_index
        self.started = False
        self.tx_buffers = {0: deque(), 1: deque()}
        self.rx_buffers = {0: deque(), 1: deque()}
        self.fail_mode = False

    def init(self, channel, **kwargs):
        pass

    def send(self, channel, msgs, timeout=None):
        if self.fail_mode:
            return False
        for msg in msgs:
            self.tx_buffers[channel].append(msg)
        return True

    def receive(self, channel):
        if self.fail_mode:
            raise can.CanError("Mocked physical adapter read failure")
        msgs = []
        while self.rx_buffers[channel]:
            msgs.append(self.rx_buffers[channel].popleft())
        return msgs

    def flush_tx_buffer(self, channel, timeout):
        self.tx_buffers[channel].clear()

    def stop(self, channel):
        self.started = False

from canalyst_manager import EnhancedCANalystIIBus

class VirtualCANalystBus(EnhancedCANalystIIBus):
    """
    A purely virtual bus that conforms to the EnhancedCANalystIIBus signature
    but overrides the hardware device with VirtualCANalystDevice.
    """
    def __init__(self, *args, **kwargs):
        self.mock_device = VirtualCANalystDevice()
        # Mock the driver before super init tries to use it
        import canalystii
        original_device = getattr(canalystii, "CanalystDevice", None)
        canalystii.CanalystDevice = lambda device_index=0: self.mock_device
        
        try:
            super().__init__(*args, **kwargs)
        finally:
            if original_device:
                canalystii.CanalystDevice = original_device

    def inject_mock_message(self, channel: int, can_id: int, data: bytes, dlc: int = None, is_error: bool = False, timestamp_ticks: int = None):
        """Helper to inject raw frames into the mock hardware buffers."""
        import canalystii
        import ctypes
        
        if dlc is None:
            dlc = len(data)
            
        padded_data = data + b'\x00' * (8 - len(data))
        
        if timestamp_ticks is None:
            # Fake some timestamp progression
            timestamp_ticks = int(time.monotonic() * 10000)

        # Build the raw C structure
        raw_msg = canalystii.Message(
            can_id,
            timestamp_ticks,
            1 if not is_error else 0, # time_flag
            0, # send_type
            0, # remote
            0, # ext
            dlc,
            (ctypes.c_ubyte * 8)(*padded_data),
        )
        # Mock error frame flag (python-can normally derives this from arbitration_id in socketcan, but we manually inject it for tests)
        if is_error:
            raw_msg.can_id |= 0x20000000 # CAN_ERR_FLAG
            
        self.mock_device.rx_buffers[channel].append(raw_msg)
