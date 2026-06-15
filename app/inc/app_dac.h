/**
 * @file app_dac.h
 * @brief DAC 输出应用模块接口。
 */
#ifndef APP_DAC_H
#define APP_DAC_H

#include <stdint.h>

typedef enum {
    APP_DAC_MODE_SINGLE = 0,
    APP_DAC_MODE_DUAL = 1
} app_dac_mode_t;

/**
 * @brief 初始化 DAC 输出模块。
 */
void app_dac_init(void);

void app_dac_set_mode(app_dac_mode_t mode);
void app_dac_set_frequency(uint32_t hz);
void app_dac_set_amplitude(uint16_t code);
void app_dac_set_phase(uint16_t degrees);

app_dac_mode_t app_dac_get_mode(void);
uint32_t app_dac_get_frequency(void);
uint16_t app_dac_get_amplitude(void);
uint16_t app_dac_get_phase(void);
uint16_t app_dac_get_sample(uint8_t channel, uint16_t index);
uint16_t app_dac_get_table_size(void);

#endif
