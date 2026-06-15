#ifndef RUST_ALGOS_H
#define RUST_ALGOS_H

/* 本文件由 cbindgen 根据 rust_algos/src/lib.rs 自动生成，请勿手工修改。 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * 获取 Rust 算法库版本号。
 *
 * 返回值格式当前由 Rust 库内部定义。
 */
uint32_t pm_algos_version(void);

/**
 * 计算功率的定点数结果。
 *
 * voltage_x100 表示电压，单位为 0.01 V。
 * current_x1000 表示电流，单位为 0.001 A。
 * 返回值表示功率，单位为 0.01 W。
 */
uint32_t pm_calc_power_x100(uint32_t voltage_x100, uint32_t current_x1000);

/**
 * ADC 算法占位接口，用于验证 Rust/C 链接。
 */
uint32_t pm_placeholder_adc_ready(void);

/**
 * DAC 算法占位接口，用于验证 Rust/C 链接。
 */
uint32_t pm_placeholder_dac_ready(void);

/**
 * PWM 算法占位接口，用于验证 Rust/C 链接。
 */
uint32_t pm_placeholder_pwm_ready(void);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* RUST_ALGOS_H */
