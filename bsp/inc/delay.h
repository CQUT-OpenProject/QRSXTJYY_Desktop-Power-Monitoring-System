/**
 * @file delay.h
 * @brief 阻塞式延时接口。
 */
#ifndef DELAY_H
#define DELAY_H

#include <stdint.h>

/**
 * @brief 初始化 SysTick 延时参数。
 */
void delay_init(void);

/**
 * @brief 延时指定微秒数。
 * @param us 延时时长，单位为微秒。
 */
void delay_us(uint32_t us);

/**
 * @brief 延时指定毫秒数。
 * @param ms 延时时长，单位为毫秒。
 */
void delay_ms(uint32_t ms);

#endif
