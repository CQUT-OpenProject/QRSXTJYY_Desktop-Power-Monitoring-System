/**
 * @file app_config.h
 * @brief 项目集中配置：板级参数、外设参数、IO 分配。
 *
 * 修改任何"硬件相关"参数只需改这一个文件，各模块 include 后直接使用。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ================================================================== */
/* Board                                                              */
/* ================================================================== */

/** 系统主频，STM32F103RCT6 默认 72 MHz。 */
#define APP_SYSCLK_HZ                  72000000U

/**
 * APB1 定时器时钟频率。
 * STM32F103 APB1 预分频 >1 时，定时器时钟 = APB1 × 2 = 72 MHz。
 */
#define APP_APB1_TIMER_CLK_HZ          72000000U

/* ================================================================== */
/* USART 协议                                                         */
/* ================================================================== */

/** USART1 波特率。 */
#define APP_UART_BAUDRATE              115200U

/* ================================================================== */
/* PWM 输出                                                           */
/* ================================================================== */

/** PWM 默认输出频率，单位 Hz。 */
#define APP_PWM_DEFAULT_HZ             50U

/** PWM 频率下限，单位 Hz。 */
#define APP_PWM_MIN_HZ                 1U

/** PWM 频率上限，单位 Hz。 */
#define APP_PWM_MAX_HZ                 100000U

/* ================================================================== */
/* DAC 输出                                                           */
/* ================================================================== */

/** DAC 波形表每周期点数（同时用于 DMA 缓冲长度）。 */
#define APP_DAC_TABLE_SIZE             128U

/** DAC 默认输出频率，单位 Hz。 */
#define APP_DAC_DEFAULT_FREQ_HZ        50U

/** DAC 默认幅度（12 位码值，0..APP_DAC_MAX_AMP）。 */
#define APP_DAC_DEFAULT_AMPLITUDE      1500U

/** DAC 最高输出频率，单位 Hz。 */
#define APP_DAC_MAX_FREQ_HZ            1000U

/** DAC 最低输出频率，单位 Hz。 */
#define APP_DAC_MIN_HZ                 1U

/** 12 位 DAC 满幅安全上限（中点 ± APP_DAC_MAX_AMP）。 */
#define APP_DAC_MAX_AMP                2047U

/** 12 位 DAC 中点码值（零电平）。 */
#define APP_DAC_MID_CODE               2048U

/* ================================================================== */
/* ADC 采样                                                           */
/* ================================================================== */

/** 每周期（50 Hz 基波）采样点数。 */
#define APP_ADC_SAMPLES                128U

/** ADC 扫描通道数（VL / iL / iLK）。 */
#define APP_ADC_CHANNELS               3U

/** ADC 触发采样率：50 Hz × 128 点/周期 = 6400 组/秒。 */
#define APP_ADC_SAMPLE_RATE_HZ         6400U

/**
 * 电压通道 ADC 通道号（PC0 / ADC1_IN10）。
 * 数值与 CMSIS ADC_Channel_10 一致。
 */
#define APP_ADC_V_CHANNEL              10U

/**
 * 电流通道 ADC 通道号（PC3 / ADC1_IN13）。
 * 数值与 CMSIS ADC_Channel_13 一致。
 */
#define APP_ADC_I_CHANNEL              13U

/**
 * 漏电流通道 ADC 通道号（PC2 / ADC1_IN12）。
 * 数值与 CMSIS ADC_Channel_12 一致。
 */
#define APP_ADC_ILK_CHANNEL            12U

/**
 * 电压通道校准增益 × 1000。
 * Vrms (0.01 V) = rms_code × GAIN_V / 1000
 * 默认 1000 表示 1:1，实物需按电压互感器/分压比设定。
 */
#define APP_ADC_CALIB_V_GAIN_X1000     17600U

/**
 * 电流通道校准增益 × 1000。
 * Irms (0.001 A) = rms_code × GAIN_I / 1000
 */
#define APP_ADC_CALIB_I_GAIN_X1000     1000U

/**
 * 漏电流通道校准增益 × 1000。
 * Ilk_rms (0.001 A) = rms_code × GAIN_ILK / 1000
 */
#define APP_ADC_CALIB_ILK_GAIN_X1000   1000U

/**
 * 电流通道极性修正系数（+1 或 -1）。
 * miniIO 副板 CT1 电流互感器输出极性与电压通道相反，
 * 软件默认取 -1 使普通负载有功功率显示为正值。
 * 仅影响有功功率符号，不影响 VRMS、IRMS 和视在功率。
 */
#define APP_ADC_I_POLARITY             (-1)

/* ================================================================== */
/* System Info                                                        */
/* ================================================================== */

/** 固件版本号字符串。 */
#define APP_FIRMWARE_VERSION           "v1.0.0"

#endif /* APP_CONFIG_H */
