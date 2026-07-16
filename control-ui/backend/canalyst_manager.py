import time
import threading
import logging
import collections
from typing import Optional, Dict, List, Tuple
import can
from can.interfaces.canalystii import CANalystIIBus
from can import Message

logger = logging.getLogger(__name__)

class EnhancedCANalystIIBus(CANalystIIBus):
    """
    An enhanced CANalyst-II bus wrapper that implements lessons from the architecture research:
    1. Tuned RX polling delay for lower latency.
    2. Strict hardware timestamp epoch mapping with 32-bit wrap detection (CANgaroo style).
    3. Payload slicing to strict DLC to remove padded junk bytes.
    4. Observable queue drops instead of silent eviction.
    """
    
    # Tune polling from 20ms down to 5ms for better bench responsiveness
    RX_POLL_DELAY = 0.005 

    def __init__(self, *args, rx_queue_size: int = 10000, **kwargs):
        # We handle the bounded queue manually so we can track drops
        super().__init__(*args, rx_queue_size=None, **kwargs)
        
        # Bounded queue with drop tracking
        self.max_queue_size = rx_queue_size
        self.drop_count = 0
        
        # Hardware timestamp mapping state
        self._host_epoch_s = time.monotonic()
        self._last_hw_timestamp = 0
        self._hw_wrap_offset = 0
        self._half_range = (1 << 31) * 100e-6 # Assuming 32-bit 100us timer wrap

    def _recv_from_queue(self) -> Tuple[Message, bool]:
        """Override to implement proper timestamp mapping and DLC slicing."""
        channel, raw_msg = self.rx_queue.popleft()

        # 1. Hardware Timestamp Mapping (CANgaroo style)
        # Protocol timestamps are in units of 100us
        hw_timestamp_s = raw_msg.timestamp * 100e-6
        
        # Detect wrap arounds (if timestamp suddenly jumps backward by a large amount)
        if self._last_hw_timestamp - hw_timestamp_s > self._half_range:
            self._hw_wrap_offset += (1 << 32) * 100e-6
            logger.warning("CANalyst-II hardware timestamp wrap-around detected.")
            
        self._last_hw_timestamp = hw_timestamp_s
        mapped_timestamp = self._host_epoch_s + hw_timestamp_s + self._hw_wrap_offset

        # 2. Strict DLC payload slicing
        # CANalyst-II pads data, we must slice to exact DLC
        dlc = raw_msg.data_len
        sliced_data = bytes(raw_msg.data)[:dlc]

        msg = Message(
            channel=channel,
            timestamp=mapped_timestamp,
            arbitration_id=raw_msg.can_id,
            is_extended_id=raw_msg.extended,
            is_remote_frame=raw_msg.remote,
            dlc=dlc,
            data=sliced_data,
        )
        return (msg, False)

    def poll_received_messages(self) -> None:
        """Override to implement observable drops on the rx_queue."""
        for channel in self.channels:
            for raw_msg in self.device.receive(channel):
                if len(self.rx_queue) >= self.max_queue_size:
                    self.drop_count += 1
                    # Evict oldest to make room
                    self.rx_queue.popleft()
                self.rx_queue.append((channel, raw_msg))


class CANState:
    """Represents the latest state of a specific CAN ID (SavvyCAN pattern)."""
    def __init__(self, msg: Message):
        self.latest_msg = msg
        self.count = 1
        self.first_seen = msg.timestamp
        self.last_seen = msg.timestamp
        self.delta_t = 0.0

    def update(self, msg: Message):
        self.delta_t = msg.timestamp - self.last_seen
        self.last_seen = msg.timestamp
        self.latest_msg = msg
        self.count += 1

    def to_dict(self) -> dict:
        return {
            "channel": self.latest_msg.channel,
            "can_id": hex(self.latest_msg.arbitration_id),
            "data": self.latest_msg.data.hex(),
            "dlc": self.latest_msg.dlc,
            "count": self.count,
            "delta_t_ms": round(self.delta_t * 1000, 2),
            "age_ms": round((time.monotonic() - self.last_seen) * 1000, 2)
        }


class CANalystManager:
    """
    Manager that encapsulates the thread boundary and UI batching logic.
    Implements immediate failure propagation and latest-ID overwrite.
    """
    def __init__(self, channels=[0, 1], bitrate=500000):
        self.channels = channels
        self.bitrate = bitrate
        self.bus: Optional[EnhancedCANalystIIBus] = None
        
        # System State
        self.is_connected = False
        self.last_error = None
        
        # Bus Error State
        self.error_frame_count = 0
        self.last_bus_error = None
        
        # Latest-ID overwrite map (channel -> id -> CANState)
        self.latest_state_map: Dict[int, Dict[int, CANState]] = {ch: {} for ch in channels}
        
        # Threading
        self._reader_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._lock = threading.Lock()

    def start(self):
        try:
            self.bus = EnhancedCANalystIIBus(channel=self.channels, bitrate=self.bitrate)
            self.is_connected = True
            self.last_error = None
            
            self._stop_event.clear()
            self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
            self._reader_thread.start()
            logger.info("CANalyst-II Manager started successfully.")
        except Exception as e:
            self._handle_failure(f"Failed to open CANalyst-II: {str(e)}")

    def stop(self):
        self._stop_event.set()
        if self._reader_thread and self._reader_thread.is_alive():
            self._reader_thread.join(timeout=1.0)
        
        if self.bus:
            self.bus.shutdown()
            self.bus = None
            
        self.is_connected = False
        logger.info("CANalyst-II Manager stopped.")

    def _handle_failure(self, error_msg: str):
        """Immediate failure propagation pattern."""
        self.is_connected = False
        self.last_error = error_msg
        logger.error(f"CANalyst-II Failure: {error_msg}")
        # Note: We do NOT auto-resume Bench TX on reconnect for safety.

    def _decode_error_frame(self, msg: Message) -> dict:
        """Decode a CAN error frame into a structured dictionary with standard SocketCAN error codes."""
        errors = []
        
        # Standard Linux SocketCAN error masks
        if msg.arbitration_id & 0x001: errors.append({"code": "ERR-001", "name": "TX Timeout"})
        if msg.arbitration_id & 0x002: errors.append({"code": "ERR-002", "name": "Lost Arbitration"})
        if msg.arbitration_id & 0x004: errors.append({"code": "ERR-004", "name": "Controller Error"})
        if msg.arbitration_id & 0x008: errors.append({"code": "ERR-008", "name": "Protocol Violation"})
        if msg.arbitration_id & 0x010: errors.append({"code": "ERR-010", "name": "Transceiver Error"})
        if msg.arbitration_id & 0x020: errors.append({"code": "ERR-020", "name": "No ACK"})
        if msg.arbitration_id & 0x040: errors.append({"code": "ERR-040", "name": "Bus Off"})
        if msg.arbitration_id & 0x080: errors.append({"code": "ERR-080", "name": "Bus Error"})
        if msg.arbitration_id & 0x100: errors.append({"code": "ERR-100", "name": "Restarted"})

        if not errors:
            errors.append({"code": "ERR-UNK", "name": "Unknown Error"})

        return {
            "errors": errors,
            "raw_id": hex(msg.arbitration_id),
            "data_hex": msg.data.hex() if len(msg.data) > 0 else None
        }

    def _read_loop(self):
        """Dedicated reader thread strictly separated from async event loops."""
        while not self._stop_event.is_set():
            try:
                msg = self.bus.recv(timeout=0.1)
                if msg:
                    with self._lock:
                        if getattr(msg, "is_error_frame", False):
                            self.error_frame_count += 1
                            self.last_bus_error = self._decode_error_frame(msg)
                            error_names = [e["name"] for e in self.last_bus_error["errors"]]
                            logger.warning(f"Bus Error Frame: {' | '.join(error_names)} (Raw ID: {self.last_bus_error['raw_id']})")
                        else:
                            channel_map = self.latest_state_map[msg.channel]
                            if msg.arbitration_id in channel_map:
                                channel_map[msg.arbitration_id].update(msg)
                            else:
                                channel_map[msg.arbitration_id] = CANState(msg)
            except can.CanError as e:
                # Immediate loss evidence (Not relying on Bus.state)
                self._handle_failure(f"Bus read error: {str(e)}")
                break
            except Exception as e:
                self._handle_failure(f"Unexpected reader exception: {str(e)}")
                break

    def send(self, msg: Message):
        """Thread-safe send with explicit failure throwing."""
        if not self.is_connected or not self.bus:
            raise RuntimeError("Cannot send, adapter is not connected.")
        
        try:
            self.bus.send(msg)
        except Exception as e:
            self._handle_failure(f"Failed to send message: {str(e)}")
            raise

    def get_ui_batch(self) -> dict:
        """
        Batched UI presentation (SavvyCAN pattern). 
        Call this at 20-30Hz from the UI backend (e.g. FastAPI route).
        """
        with self._lock:
            state = {
                "connected": self.is_connected,
                "system_error": self.last_error,
                "bus_error": self.last_bus_error,
                "error_frame_count": self.error_frame_count,
                "dropped_frames": self.bus.drop_count if self.bus else 0,
                "channels": {}
            }
            
            for ch in self.channels:
                ch_state = {}
                for can_id, can_state in self.latest_state_map[ch].items():
                    ch_state[hex(can_id)] = can_state.to_dict()
                state["channels"][ch] = ch_state
                
            return state

# Example usage
if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    manager = CANalystManager(channels=[0, 1], bitrate=500000)
    manager.start()
    
    try:
        # Simulate the UI polling at 20Hz
        for _ in range(20):
            time.sleep(0.05)
            batch = manager.get_ui_batch()
            if batch["system_error"]:
                print(f"System Error: {batch['system_error']}")
                break
            if batch["bus_error"]:
                error_names = [e["name"] for e in batch["bus_error"]["errors"]]
                print(f"Bus Error: {' | '.join(error_names)}")
            print(f"Dropped frames: {batch['dropped_frames']}")
    finally:
        manager.stop()
