/**
 * @file app_adc.h
 * @brief ADC 采样与电气参数计算模块接口。
 *
 * PC0/PC3/PC2 作为 ADC1_IN10/IN13/IN12，TIM3 TRGO 以 6400 Hz 触发 3 通道
 * 规则组扫描。DMA1_Channel1 以 2 倍帧长循环搬运，HT/TC 中断分别在半帧和
 * 整帧完成时解交错到三个独立通道数组并通知主循环。
 *
 * 主循环中的 app_adc_task() 检查新数据标志，将样点复制到局部缓冲区后调用
 * Rust 定点数算法库 pm_calc_electrical() 计算电参数并更新内部快照。
 */
#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>

/** 每周期（50 Hz 基波）采样点数。 */
#define APP_ADC_SAMPLES 128U

/** ADC 扫描通道数（VL / iL / iLK）。 */
#define APP_ADC_CHANNELS 3U

/* ------------------------------------------------------------------ */
/* 校准增益宏（实物调试时按实际模拟前端参数调整）                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 电压通道校准增益 × 1000。
 *
 * Vrms (0.01 V) = rms_code × GAIN_V / 1000
 * 默认 1000 表示 1:1（不做缩放），实物需按电压互感器/分压比设定。
 */
#define APP_ADC_CALIB_V_GAIN_X1000    1000U

/**
 * @brief 电流通道校准增益 × 1000。
 *
 * Irms (0.001 A) = rms_code × GAIN_I / 1000
 */
#define APP_ADC_CALIB_I_GAIN_X1000    1000U

/**
 * @brief 漏电流通道校准增益 × 1000。
 *
 * Ilk_rms (0.001 A) = rms_code × GAIN_ILK / 1000
 */
#define APP_ADC_CALIB_ILK_GAIN_X1000  1000U

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
    /** 有功功率 × 10（单位 0.1 W） */
    uint32_t active_power_x10;
    /** 视在功率 × 10（单位 0.1 VA） */
    uint32_t apparent_power_x10;
    /** 功率因数 × 1000（范围 0..1000，例如 810 表示 0.810） */
    uint16_t power_factor_x1000;
    /** 是否已执行过 CAL ZERO 校准（1 = 已校准） */
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
 * 检查 DMA TC 中断设置的 s_adc_fresh 标志，如果有新数据则复制到局部缓冲区、
 * 调用 Rust 算法库计算电参数并更新内部快照。
 */
void app_adc_task(void);

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
 * @brief 获取指定通道最近一轮的原始 ADC 样点数组。
 *
 * @param channel 0 = 电压(PC0/ADC1_IN10), 1 = 电流(PC3/ADC1_IN13),
 *                2 = 漏电流(PC2/ADC1_IN12)
 * @return 指向 128 半字静态数组的指针，channel 非法时返回 NULL。
 */
const uint16_t *app_adc_get_samples(uint8_t channel);

/**
 * @brief 执行零偏移校准。
 *
 * 用当前 s_adc_v/i/ilk 各 128 点的算术平均值更新内部 DC 偏移补偿值。
 * 实物流调零时通过 CAL ZERO 串口命令触发。
 */
void app_adc_calibrate_zero(void);

#endif
