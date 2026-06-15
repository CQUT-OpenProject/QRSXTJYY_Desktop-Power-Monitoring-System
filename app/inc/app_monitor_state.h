/**
 * @file app_monitor_state.h
 * @brief 监控状态快照接口。
 */
#ifndef APP_MONITOR_STATE_H
#define APP_MONITOR_STATE_H

#include <stdint.h>

#define APP_MONITOR_WAVEFORM_SAMPLES 128U

typedef struct {
    uint32_t frequency_x100;
} app_monitor_mains_frequency_t;

typedef struct {
    uint32_t frequency_hz;
} app_monitor_pwm_output_t;

typedef struct {
    uint8_t dual_output_enabled;
    uint32_t frequency_hz;
    uint16_t amplitude;
    uint16_t phase_degrees;
    uint16_t waveform_sample_count;
    uint16_t waveform_ch1[APP_MONITOR_WAVEFORM_SAMPLES];
    uint16_t waveform_ch2[APP_MONITOR_WAVEFORM_SAMPLES];
} app_monitor_dac_output_t;

typedef struct {
    uint32_t now_us;
    app_monitor_mains_frequency_t mains_frequency;
    app_monitor_pwm_output_t pwm_output;
    app_monitor_dac_output_t dac_output;
} app_monitor_state_t;

/**
 * @brief 读取一次供 LCD 和串口状态输出使用的监控读模型。
 */
void app_monitor_state_read(app_monitor_state_t *state);

#endif
