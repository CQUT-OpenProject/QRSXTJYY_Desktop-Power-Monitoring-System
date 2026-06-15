#![cfg_attr(not(test), no_std)]

#[cfg(not(test))]
use core::panic::PanicInfo;

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// 获取 Rust 算法库版本号。
///
/// 返回值格式当前由 Rust 库内部定义。
#[no_mangle]
pub extern "C" fn pm_algos_version() -> u32 {
    0x0001_0000
}

/// 计算功率的定点数结果。
///
/// voltage_x100 表示电压，单位为 0.01 V。
/// current_x1000 表示电流，单位为 0.001 A。
/// 返回值表示功率，单位为 0.01 W。
#[no_mangle]
pub extern "C" fn pm_calc_power_x100(voltage_x100: u32, current_x1000: u32) -> u32 {
    voltage_x100.saturating_mul(current_x1000) / 1000
}

/// ADC 算法占位接口，用于验证 Rust/C 链接。
#[no_mangle]
pub extern "C" fn pm_placeholder_adc_ready() -> u32 {
    0
}

/// DAC 算法占位接口，用于验证 Rust/C 链接。
#[no_mangle]
pub extern "C" fn pm_placeholder_dac_ready() -> u32 {
    0
}

/// PWM 算法占位接口，用于验证 Rust/C 链接。
#[no_mangle]
pub extern "C" fn pm_placeholder_pwm_ready() -> u32 {
    0
}
