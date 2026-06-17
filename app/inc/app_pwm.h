/**
 * @file app_pwm.h
 * @brief PWM 输出模块接口。
 *
 * PA8/TIM1_CH1 输出 50% 占空比方波，
 * 支持 1..100000 Hz 范围内频率可调。
 */
#ifndef APP_PWM_H
#define APP_PWM_H

#include <stdint.h>

/**
 * @brief 初始化 PA8/TIM1_CH1 PWM 输出，默认 50 Hz。
 */
void app_pwm_init(void);

/**
 * @brief 设置 PWM 输出频率并返回实际生效值。
 *
 * @param hz 目标频率，范围 1..100000。超范围值会被夹住。
 * @return 实际采用的频率，单位 Hz。
 */
uint32_t app_pwm_set_frequency(uint32_t hz);

/**
 * @brief 获取当前 PWM 输出频率。
 */
uint32_t app_pwm_get_frequency(void);

#endif
