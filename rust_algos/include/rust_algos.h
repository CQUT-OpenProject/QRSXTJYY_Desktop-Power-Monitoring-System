#ifndef RUST_ALGOS_H
#define RUST_ALGOS_H

/* 本文件由 cbindgen 根据 rust_algos/src/lib.rs 自动生成，请勿手工修改。 */

#include <stdint.h>

/**
 * 定点数电参数计算结果。
 *
 * 所有物理量使用定点整数表示，避免浮点运算。
 * 布局与 C 侧 app_electrical_params_t 一致。
 */
typedef struct PmElectricalResult {
  /**
   * 电压真有效值 × 100（单位 0.01 V）
   */
  uint32_t vrms_x100;
  /**
   * 电流真有效值 × 1000（单位 0.001 A）
   */
  uint32_t irms_x1000;
  /**
   * 漏电流真有效值 × 1000（单位 0.001 A）
   */
  uint32_t ilk_rms_x1000;
  /**
   * 有功功率 × 10（单位 0.1 W）
   */
  uint32_t active_power_x10;
  /**
   * 视在功率 × 10（单位 0.1 VA）
   */
  uint32_t apparent_power_x10;
  /**
   * 功率因数 × 1000（范围 0..1000，例如 810 表示 0.810）
   */
  uint16_t power_factor_x1000;
  /**
   * 保留对齐
   */
  uint16_t reserved;
} PmElectricalResult;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * 计算功率的定点数结果。
 *
 * voltage_x100 表示电压，单位为 0.01 V。
 * current_x1000 表示电流，单位为 0.001 A。
 * 返回值表示功率，单位为 0.01 W。
 */
uint32_t pm_calc_power_x100(uint32_t voltage_x100, uint32_t current_x1000);

/**
 * 从 ADC 原始样点计算单相电参数。
 *
 * # 参数
 *
 * * `v_samples` / `i_samples` / `ilk_samples` — 三通道原始 ADC 码值数组，各 count 个元素。
 * * `count` — 样点数（通常为 128）。
 * * `v_gain_x1000` 等 — 校准增益 × 1000，将 ADC RMS 码值换算为物理量。
 *   增益含义：physical_value = rms_code × gain / 1000。
 * * `v_zero` 等 — DC 偏移（0 表示从本批数据自动估算）。
 * * `result` — 输出电参数结构体。
 *
 * # 算法
 *
 * 1. 对每通道求 DC 偏移（手动指定或自动估算均值）。
 * 2. 去直流后计算 RMS 码值。
 * 3. RMS 码值 × 增益 → 物理量 RMS。
 * 4. 有功功率 = mean( (v[n]-Vdc) × (i[n]-Idc) )，再乘增益。
 * 5. 视在功率 = Vrms × Irms。
 * 6. 功率因数 = P / S，钳位到 [0, 1] 区间。
 */
void pm_calc_electrical(const uint16_t *v_samples,
                        const uint16_t *i_samples,
                        const uint16_t *ilk_samples,
                        uint16_t count,
                        uint32_t v_gain_x1000,
                        uint32_t i_gain_x1000,
                        uint32_t ilk_gain_x1000,
                        uint16_t v_zero,
                        uint16_t i_zero,
                        uint16_t ilk_zero,
                        struct PmElectricalResult *result);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* RUST_ALGOS_H */
