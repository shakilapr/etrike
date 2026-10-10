#![no_std]
#![no_main]

//! RT firmware entry point (ESP32-S3). Initialises the HAL (TWAI Low bus +
//! MCP2515 High bus) and runs the control loop.

use esp_hal::clock::CpuClock;
use esp_hal::timer::timg::TimerGroup;

use rt_esp32_rs::hal::RtHalImpl;
use rt_esp32_rs::{run, RtState};

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

esp_bootloader_esp_idf::esp_app_desc!();

#[esp_rtos::main]
async fn main(_spawner: embassy_executor::Spawner) -> ! {
    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0, peripherals.FROM_CPU_INTR0);

    let mut hal = RtHalImpl::new(
        peripherals.TWAI0,
        peripherals.GPIO4,
        peripherals.GPIO5,
        peripherals.SPI2,
        peripherals.GPIO15,
        peripherals.GPIO16,
        peripherals.GPIO17,
        peripherals.GPIO18,
    );
    let mut state = RtState::default();
    run(&mut hal, &mut state)
}
