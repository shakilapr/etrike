//! ESP32-S3 hardware binding for SYS (feature `hal`, target-only).
//!
//! Owns the on-chip TWAI (Low bus), the debounced safety/body inputs, and the
//! control tick. All decision logic stays in `etrike_core`/the shell; this file
//! only moves bytes and pin levels.

use esp_hal::delay::Delay;
use esp_hal::gpio::interconnect::{PeripheralInput, PeripheralOutput};
use esp_hal::gpio::{Input, InputConfig, InputPin, Pull};
use esp_hal::time::Instant;
use esp_hal::twai::{
    BaudRate, EspTwaiFrame, Id, Instance, StandardId, Twai, TwaiConfiguration, TwaiMode,
};
use esp_hal::Blocking;

use embedded_can::{Frame, Id as CanId};

use etrike_protocol::{Bus, RawFrame};

use crate::{GpioState, SysHal};

/// Control-loop period in milliseconds.
const TICK_MS: u32 = 10;

pub struct SysHalImpl {
    twai: Twai<'static, Blocking>,
    delay: Delay,
    start: Instant,
    estop: Input<'static>,
    start_btn: Input<'static>,
    lever: Input<'static>,
    mode_btn: Input<'static>,
    sw_left: Input<'static>,
    sw_right: Input<'static>,
    sw_head: Input<'static>,
}

fn input(pin: impl InputPin + 'static) -> Input<'static> {
    Input::new(pin, InputConfig::default().with_pull(Pull::Up))
}

impl SysHalImpl {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        twai: impl Instance + 'static,
        rx: impl PeripheralInput<'static>,
        tx: impl PeripheralOutput<'static>,
        estop: impl InputPin + 'static,
        start_btn: impl InputPin + 'static,
        lever: impl InputPin + 'static,
        mode_btn: impl InputPin + 'static,
        sw_left: impl InputPin + 'static,
        sw_right: impl InputPin + 'static,
        sw_head: impl InputPin + 'static,
    ) -> Self {
        let twai = TwaiConfiguration::new(twai, rx, tx, BaudRate::B500K, TwaiMode::Normal).start();
        Self {
            twai,
            delay: Delay::new(),
            start: Instant::now(),
            estop: input(estop),
            start_btn: input(start_btn),
            lever: input(lever),
            mode_btn: input(mode_btn),
            sw_left: input(sw_left),
            sw_right: input(sw_right),
            sw_head: input(sw_head),
        }
    }
}

impl SysHal for SysHalImpl {
    fn now_ms(&mut self) -> u64 {
        self.start.elapsed().as_millis()
    }

    fn poll_frame(&mut self) -> Option<RawFrame> {
        match self.twai.receive() {
            Ok(frame) => {
                let id = match frame.id() {
                    CanId::Standard(standard) => standard.as_raw() as u32,
                    CanId::Extended(extended) => extended.as_raw(),
                };
                RawFrame::from_slice(Bus::Low, id, frame.data())
            }
            Err(_) => None,
        }
    }

    fn send_frame(&mut self, frame: &RawFrame) {
        let Some(id) = StandardId::new(frame.id as u16) else {
            return;
        };
        if let Some(message) = EspTwaiFrame::new(Id::Standard(id), frame.payload()) {
            let _ = self.twai.transmit(&message);
        }
    }

    fn gpio(&mut self) -> GpioState {
        GpioState {
            // ESTOP is an NC contact: open/pressed reads HIGH (fail-safe).
            estop_pressed: self.estop.is_high(),
            // START is an NC latching run/enable control: latched reads HIGH.
            start_run: self.start_btn.is_high(),
            // Lever is active-low with a pull-up: pressed reads LOW.
            brake_lever: self.lever.is_low(),
            mode_long_press: self.mode_btn.is_low(),
            switch_left: self.sw_left.is_low(),
            switch_right: self.sw_right.is_low(),
            switch_head: self.sw_head.is_low(),
        }
    }

    fn wait_next_tick(&mut self) {
        self.delay.delay_millis(TICK_MS);
    }
}
