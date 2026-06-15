/**
 * @file app_capture.h
 * @brief 输入捕获应用模块接口。
 */
#ifndef APP_CAPTURE_H
#define APP_CAPTURE_H

#include <stdint.h>

/**
 * @brief 初始化输入捕获模块。
 */
void app_capture_init(void);

/**
 * @brief 输入捕获周期任务，处理超时和上报节流。
 *
 * 中断只负责快速记录捕获结果；超时判断、是否需要自动上报这些较慢逻辑
 * 放在主循环里执行，避免中断处理时间太长。
 */
void app_capture_task(void);

/**
 * @brief TIM2 中断入口转发函数。
 */
void app_capture_tim2_irq_handler(void);

/**
 * @brief 获取最新测得频率，单位为 Hz * 100。
 *
 * 使用 Hz * 100 可以在没有浮点数的情况下表示两位小数，例如 50.12 Hz
 * 保存为 5012。
 */
uint32_t app_capture_get_frequency_x100(void);

/**
 * @brief 获取 TIM2 扩展后的微秒时间戳。
 *
 * TIM2 是 16 位计数器，溢出后会从 0 重新计数；本文件用 s_overflows 记录
 * 溢出次数，再拼成 32 位微秒时间。
 */
uint32_t app_capture_get_time_us(void);

/**
 * @brief 取出一次待上报的测量结果。
 * @return 1 表示有新结果，0 表示无。
 */
uint8_t app_capture_take_report(uint32_t *freq_x100);

#endif
