#![no_std]
#![no_main]

//! SYS firmware entry point (ESP32-S3). Initialises the HAL, constructs the
//! `SysCore`, and runs the control loop.

use esp_hal::clock::CpuClock;
use esp_hal::timer::timg::TimerGroup;

use etrike_core::sys::{SysConfig, SysCore};
use sys_esp32_rs::hal::SysHalImpl;
use sys_esp32_rs::run;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

// Default esp-idf application descriptor.
esp_bootloader_esp_idf::esp_app_desc!();

// Bench safety bypass: pass every command straight through. Set to `false` for
// a safety-enforcing build.
const SAFETY_BYPASS: bool = true;

#[esp_rtos::main]
async fn main(_spawner: embassy_executor::Spawner) -> ! {
    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0, peripherals.FROM_CPU_INTR0);

    let mut hal = SysHalImpl::new(
        peripherals.TWAI0,
        peripherals.GPIO4,
        peripherals.GPIO5,
        peripherals.GPIO1,
        peripherals.GPIO41,
        peripherals.GPIO2,
        peripherals.GPIO11,
        peripherals.GPIO9,
        peripherals.GPIO6,
        peripherals.GPIO7,
    );
    let mut core = SysCore::new(SysConfig {
        bypass: SAFETY_BYPASS,
        ..SysConfig::default()
    });
    run(&mut hal, &mut core, SAFETY_BYPASS)
}
