/**
 * @file app_pwm.h
 * @brief PWM 输出应用模块接口。
 */
#ifndef APP_PWM_H
#define APP_PWM_H

#include <stdint.h>

/**
 * @brief 初始化 PWM 输出模块。
 */
void app_pwm_init(void);

/**
 * @brief 设置 PA8/TIM1_CH1 PWM 频率，保持 50% 占空比。
 *
 * 调用者只给目标频率；PSC 和 ARR 这些定时器参数在函数里算好。
 *
 * @param hz 目标频率，范围 1..100000 Hz。
 * @return 实际采用的频率参数，单位 Hz。
 */
uint32_t app_pwm_set_frequency(uint32_t hz);

/**
 * @brief 获取当前 PWM 设定频率。
 */
uint32_t app_pwm_get_frequency(void);

#endif
