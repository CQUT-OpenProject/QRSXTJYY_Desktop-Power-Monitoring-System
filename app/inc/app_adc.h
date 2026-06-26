/**
 * @file app_adc.h
 * @brief ADC 采样与电气参数计算模块接口。
 *
 * 三层快照架构（解决 DMA ISR / 主循环 / LCD-串口 数据竞争）：
 *   第一层  DMA HT/TC 中断 → deinterleave_frame() 写 ISR 双缓冲
 *   第二层  app_adc_task() 极短临界区取 ready idx → 工作帧 → Rust → 显示帧
 *   第三层  app_adc_get_samples() 等接口统一读显示帧
 *
 * PC0/PC3/PC2 作为 ADC1_IN10/IN13/IN12，TIM3 TRGO 以 6400 Hz 触发 3 通道
 * 规则组扫描。DMA1_Channel1 以 2 倍帧长循环搬运，HT/TC 中断分别在半帧和
 * 整帧完成时解交错到双缓冲并通知主循环。
 */
#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include "app_config.h"

/* ------------------------------------------------------------------ */
/* 类型定义                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief 单相交流电参数定点数结果。
 *
 * 布局与 Rust 侧 struct PmElectricalResult 一致，
 * 由 cbindgen 生成的 rust_algos.h 中同名结构体保证 ABI 兼容。
 */
typedef struct {
    /** 电压真有效值 × 100（单位 0.01 V） */
    uint32_t vrms_x100;
    /** 电流真有效值 × 1000（单位 0.001 A） */
    uint32_t irms_x1000;
    /** 漏电流真有效值 × 1000（单位 0.001 A） */
    uint32_t ilk_rms_x1000;
    /** 有功功率 × 10（单位 0.1 W），有符号：正=消耗，负=回馈 */
    int32_t  active_power_x10;
    /** 视在功率 × 10（单位 0.1 VA） */
    uint32_t apparent_power_x10;
    /** 功率因数 × 1000（范围 0..1000，例如 810 表示 0.810） */
    uint16_t power_factor_x1000;
    /** 是否已锁定上电零偏（1 = 已锁定） */
    uint8_t  zero_calibrated;
    /** 保留对齐 */
    uint8_t  reserved;
} app_electrical_params_t;

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化 ADC 采样模块。
 *
 * 按以下顺序完成后台硬件配置：
 * RCC → GPIO(PC0/PC3/PC2 模拟输入) → TIM3(6400 Hz TRGO)
 * → ADC1(扫描模式，T3_TRGO 触发) → DMA1_Channel1(2倍帧循环搬运)
 * → NVIC(DMA HT/TC 中断) → 启动 DMA 和 TIM3。
 */
void app_adc_init(void);

/**
 * @brief ADC 模块周期任务，在主循环中调用。
 *
 * 极短临界区（关中断仅取 ready 帧编号）→ 整帧复制到工作帧 → 调用 Rust 算法
 * → 更新显示帧快照，供 LCD/串口/CAL ZERO 安全读取。
 *
 * @return 1 表示更新了新的一帧数据，0 表示无新数据。
 */
uint8_t app_adc_task(void);

/**
 * @brief DMA1_Channel1 半传输/传输完成中断入口转发函数。
 *
 * 由 stm32f10x_it.c 中的 DMA1_Channel1_IRQHandler 调用。
 * HT 中断处理前半帧，TC 中断处理后半帧；各自解交错对应半区到三通道数组。
 */
void app_adc_dma1_ch1_irq_handler(void);

/**
 * @brief 获取最近一次计算得到的电参数。
 *
 * @return 指向静态存储的电参数结构体指针（只读，持续有效）。
 */
const app_electrical_params_t *app_adc_get_params(void);

/**
 * @brief 获取指定通道最近一轮的稳定样点快照（来自显示帧，不受 ISR 干扰）。
 *
 * @param channel 0 = 电压(PC0/ADC1_IN10), 1 = 电流(PC3/ADC1_IN13),
 *                2 = 漏电流(PC2/ADC1_IN12)
 * @return 指向 128 半字静态数组的指针（持续有效，不受 ISR 写入影响），
 *         channel 非法时返回 NULL。
 */
const uint16_t *app_adc_get_samples(uint8_t channel);

#endif
