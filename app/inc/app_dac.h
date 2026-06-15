/**
 * @file app_dac.h
 * @brief DAC 输出应用模块接口。
 */
#ifndef APP_DAC_H
#define APP_DAC_H

#include <stdint.h>

/**
 * @brief DAC 波形表长度。
 *
 * 128 个点组成一个完整正弦周期。点数再多一些会更平滑，但 DMA 每轮也要
 * 搬更多数据；本项目先用 128 点。
 */
#define APP_DAC_WAVEFORM_SAMPLES 128U

typedef enum {
    /** 单通道正弦波输出，第二路保持中点电压。 */
    APP_DAC_MODE_SINGLE = 0,

    /** 双通道正弦波输出，第二路可以相对第一路带相位差。 */
    APP_DAC_MODE_DUAL = 1
} app_dac_mode_t;

/**
 * @brief 用户希望 DAC 输出的波形参数。
 */
typedef struct {
    /** 输出模式：单通道或双通道。 */
    app_dac_mode_t mode;

    /** 正弦波频率，单位 Hz。 */
    uint32_t frequency_hz;

    /** 幅度，使用 12 位 DAC 码值表示，写入时会限制到安全范围。 */
    uint16_t amplitude;

    /** 第二通道相对第一通道的相位差，单位度，写入时会折算到 0..359。 */
    uint16_t phase_degrees;
} app_dac_config_t;

/**
 * @brief DAC 当前输出状态。
 *
 * config 是当前生效的设置；waveform_ch1/ch2 给 LCD 或调试界面画预览图。
 * 调用者不用关心这些点后来怎样送进 DMA 和 TIM6。
 */
typedef struct {
    app_dac_config_t config;
    uint16_t waveform_sample_count;
    uint16_t waveform_ch1[APP_DAC_WAVEFORM_SAMPLES];
    uint16_t waveform_ch2[APP_DAC_WAVEFORM_SAMPLES];
} app_dac_output_t;

/**
 * @brief 初始化 DAC 输出模块。
 */
void app_dac_init(void);

/**
 * @brief 读取当前 DAC 输出配置。
 */
void app_dac_get_config(app_dac_config_t *config);

/**
 * @brief 应用一组新的 DAC 输出配置。
 *
 * 写入配置后，会重新计算波形表，并重新启动 DMA 和 TIM6。
 */
void app_dac_apply_config(const app_dac_config_t *config);

/**
 * @brief 读取当前 DAC 输出状态和预览波形。
 */
void app_dac_read_output(app_dac_output_t *output);

#endif
