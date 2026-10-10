//! ESP32-S3 hardware binding for RT (feature `hal`, target-only).
//!
//! Owns the on-chip TWAI (Low bus), the external MCP2515 (High bus, over SPI),
//! and the control tick. All decision logic stays in `etrike_core::rt` / the
//! shell.

use embedded_can::{Frame, Id as CanId};

use esp_hal::delay::Delay;
use esp_hal::gpio::interconnect::{PeripheralInput, PeripheralOutput};
use esp_hal::gpio::{Level, Output, OutputConfig};
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::spi::Mode;
use esp_hal::time::{Instant, Rate};
use esp_hal::twai::{
    BaudRate, EspTwaiFrame, Id, Instance, StandardId, Twai, TwaiConfiguration, TwaiMode,
};
use esp_hal::Blocking;

use etrike_protocol::{Bus, RawFrame};

use crate::RtHal;

const TICK_MS: u32 = 5;

// ── MCP2515 register map (subset) ────────────────────────────────────
const MCP_RESET: u8 = 0xC0;
const MCP_READ: u8 = 0x03;
const MCP_WRITE: u8 = 0x02;
const MCP_RTS_TX0: u8 = 0x81;
const MCP_CNF1: u8 = 0x2A;
const MCP_CNF2: u8 = 0x29;
const MCP_CNF3: u8 = 0x28;
const MCP_CANCTRL: u8 = 0x0F;
const MCP_TXB0SIDH: u8 = 0x31;
const MCP_TXB0DLC: u8 = 0x35;
const MCP_TXB0D0: u8 = 0x36;
const MCP_RXB0SIDH: u8 = 0x61;
const MCP_RXB0DLC: u8 = 0x65;
const MCP_RXB0D0: u8 = 0x66;
const MCP_RX0IF: u8 = 0x01; // bit in CANINTF (0x2C)
const MCP_CANINTF: u8 = 0x2C;

/// Minimal blocking MCP2515 driver over SPI.
struct Mcp2515 {
    spi: Spi<'static, Blocking>,
    cs: Output<'static>,
}

impl Mcp2515 {
    fn new(spi: Spi<'static, Blocking>, cs: Output<'static>) -> Self {
        let mut device = Self { spi, cs };
        device.init();
        device
    }

    fn transfer(&mut self, bytes: &mut [u8]) {
        self.cs.set_low();
        let _ = self.spi.transfer(bytes);
        self.cs.set_high();
    }

    fn read_register(&mut self, address: u8) -> u8 {
        let mut buffer = [MCP_READ, address, 0x00];
        self.transfer(&mut buffer);
        buffer[2]
    }

    fn write_register(&mut self, address: u8, value: u8) {
        let mut buffer = [MCP_WRITE, address, value];
        self.transfer(&mut buffer);
    }

    fn init(&mut self) {
        self.cs.set_low();
        let _ = self.spi.write(&[MCP_RESET]);
        self.cs.set_high();
        self.delay_ms(2);
        // 500 kbit/s @ 8 MHz oscillator.
        self.write_register(MCP_CNF1, 0x00);
        self.write_register(MCP_CNF2, 0x90);
        self.write_register(MCP_CNF3, 0x02);
        // Normal mode.
        self.write_register(MCP_CANCTRL, 0x00);
    }

    fn delay_ms(&mut self, ms: u32) {
        Delay::new().delay_millis(ms);
    }

    fn receive(&mut self) -> Option<RawFrame> {
        // Only RXB0 is used: check RX0IF in CANINTF.
        let flags = self.read_register(MCP_CANINTF);
        if flags & MCP_RX0IF == 0 {
            return None;
        }
        let sidh = self.read_register(MCP_RXB0SIDH);
        let sidl = self.read_register(MCP_RXB0SIDH + 1);
        let id = ((sidh as u32) << 3) | ((sidl as u32) >> 5);
        let dlc = (self.read_register(MCP_RXB0DLC) & 0x0F) as usize;
        let mut data = [0u8; 8];
        for (index, slot) in data.iter_mut().enumerate().take(dlc.min(8)) {
            *slot = self.read_register(MCP_RXB0D0 + index as u8);
        }
        // Clear RX0IF via BIT_MODIFY(CANINTF, RX0IF, 0).
        let mut clear = [0x05, MCP_CANINTF, MCP_RX0IF, 0x00];
        self.transfer(&mut clear);
        RawFrame::from_slice(Bus::High, id, &data[..dlc.min(8)])
    }

    fn transmit(&mut self, frame: &RawFrame) {
        let id = frame.id & 0x7FF;
        self.write_register(MCP_TXB0SIDH, (id >> 3) as u8);
        self.write_register(MCP_TXB0SIDH + 1, ((id << 5) & 0xE0) as u8);
        self.write_register(MCP_TXB0DLC, frame.dlc & 0x0F);
        for (index, byte) in frame.payload().iter().enumerate() {
            self.write_register(MCP_TXB0D0 + index as u8, *byte);
        }
        self.cs.set_low();
        let _ = self.spi.write(&[MCP_RTS_TX0]);
        self.cs.set_high();
    }
}

pub struct RtHalImpl {
    low: Twai<'static, Blocking>,
    high: Mcp2515,
    delay: Delay,
    start: Instant,
}

impl RtHalImpl {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        twai: impl Instance + 'static,
        can_rx: impl PeripheralInput<'static>,
        can_tx: impl PeripheralOutput<'static>,
        spi: impl esp_hal::spi::master::Instance + 'static,
        sck: impl PeripheralOutput<'static>,
        mosi: impl PeripheralOutput<'static>,
        miso: impl PeripheralInput<'static>,
        cs: impl esp_hal::gpio::OutputPin + 'static,
    ) -> Self {
        let low =
            TwaiConfiguration::new(twai, can_rx, can_tx, BaudRate::B500K, TwaiMode::Normal).start();
        let spi = Spi::new(
            spi,
            SpiConfig::default()
                .with_frequency(Rate::from_mhz(8))
                .with_mode(Mode::_0),
        )
        .unwrap()
        .with_sck(sck)
        .with_mosi(mosi)
        .with_miso(miso);
        let cs = Output::new(cs, Level::High, OutputConfig::default());
        Self {
            low,
            high: Mcp2515::new(spi, cs),
            delay: Delay::new(),
            start: Instant::now(),
        }
    }
}

impl RtHal for RtHalImpl {
    fn now_ms(&mut self) -> u64 {
        self.start.elapsed().as_millis()
    }

    fn poll_frame(&mut self) -> Option<RawFrame> {
        if let Ok(frame) = self.low.receive() {
            let id = match frame.id() {
                CanId::Standard(standard) => standard.as_raw() as u32,
                CanId::Extended(extended) => extended.as_raw(),
            };
            return RawFrame::from_slice(Bus::Low, id, frame.data());
        }
        self.high.receive()
    }

    fn send_frame(&mut self, frame: &RawFrame) {
        match frame.bus {
            Bus::High | Bus::Powertrain => self.high.transmit(frame),
            Bus::Low => {
                let Some(id) = StandardId::new(frame.id as u16) else {
                    return;
                };
                if let Some(message) = EspTwaiFrame::new(Id::Standard(id), frame.payload()) {
                    let _ = self.low.transmit(&message);
                }
            }
        }
    }

    fn wait_next_tick(&mut self) {
        self.delay.delay_millis(TICK_MS);
    }
}
