/**
 * @file app_dac.h
 * @brief DAC 波形输出模块接口。
 *
 * 驱动 PA4(DAC_OUT1)/PA5(DAC_OUT2) 输出正弦波。
 * 通过 TIM6 TRGO 触发 DMA 循环搬运波形表到 DAC->DHR12R1/DHR12R2。
 * 支持单通道（CH2 保持中点电压）和双通道相差输出。
 */
#ifndef APP_DAC_H
#define APP_DAC_H

#include <stdint.h>
#include "app_config.h"

typedef enum {
    /** 单通道正弦波输出，第二路保持中点电压。 */
    APP_DAC_MODE_SINGLE = 0,

    /** 双通道正弦波输出，第二路相对第一路带相位差。 */
    APP_DAC_MODE_DUAL = 1
} app_dac_mode_t;

/**
 * @brief DAC 输出配置参数。
 */
typedef struct {
    app_dac_mode_t mode;          /**< 单通道或双通道。 */
    uint32_t frequency_hz;        /**< 正弦波频率，单位 Hz。 */
    uint16_t amplitude;           /**< 幅度（12 位 DAC 码值 0..2047）。 */
    uint16_t phase_degrees;       /**< 第二通道相位差，单位度，0..359。 */
} app_dac_config_t;

/**
 * @brief DAC 当前状态与波形预览。
 *
 * waveform_ch1/ch2 供 LCD 绘制波形图，调用者无需关心 DMA 和 TIM6 细节。
 */
typedef struct {
    app_dac_config_t config;
    uint16_t waveform_sample_count;
    uint16_t waveform_ch1[APP_DAC_TABLE_SIZE];
    uint16_t waveform_ch2[APP_DAC_TABLE_SIZE];
} app_dac_output_t;

/**
 * @brief 初始化 PA4/PA5 DAC 双通道输出。
 */
void app_dac_init(void);

/**
 * @brief 读取当前 DAC 配置。
 */
void app_dac_get_config(app_dac_config_t *config);

/**
 * @brief 应用新配置并重新启动波形输出。
 *
 * 重建波形表、重新配置 DMA 和 TIM6。调用后 DAC 输出立即切换到新参数。
 */
void app_dac_apply_config(const app_dac_config_t *config);

/**
 * @brief 读取当前 DAC 配置和预览波形。
 */
void app_dac_read_output(app_dac_output_t *output);

#endif
