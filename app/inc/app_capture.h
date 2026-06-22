/**
 * @file app_capture.h
 * @brief TIM2 输入捕获测频模块接口。
 *
 * PA1 上升沿捕获 → 相邻边沿时间差 → 频率 (Hz × 100)。
 * TIM2 同时提供 32 位微秒级时间戳（16 位计数器 + 溢出扩展）。
 */
#ifndef APP_CAPTURE_H
#define APP_CAPTURE_H

#include <stdint.h>

/**
 * @brief 初始化 PA1/TIM2_CH2 输入捕获和微秒时间基准。
 *
 * 配置 TIM2 以 1 MHz 运行，溢出中断扩展时间高位。
 */
void app_capture_init(void);

/**
 * @brief 输入捕获周期任务，在主循环中调用。
 *
 * 检查输入信号超时（2 秒无边沿 → 频率清零）；
 * 管理自动上报节流（周期 1 秒 / 变化 200 ms）。
 */
void app_capture_task(void);

/**
 * @brief TIM2 中断入口转发函数（捕获 + 溢出）。
 *
 * 由 stm32f10x_it.c 中的 TIM2_IRQHandler 调用。
 */
void app_capture_tim2_irq_handler(void);

/**
 * @brief 获取最新测得频率，单位 Hz × 100。
 *
 * 例如 50.00 Hz 返回 5000，避免浮点数。
 */
uint32_t app_capture_get_frequency_x100(void);

/**
 * @brief 获取 TIM2 扩展后的 32 位微秒时间戳。
 *
 * 16 位硬件计数器 + 软件溢出计数拼接，毛刺通过补偿修正。
 */
uint32_t app_capture_get_time_us(void);

/**
 * @brief 取出一条待上报的频率测量结果。
 *
 * @return 1 表示有新结果待上报，0 表示无。
 */
uint8_t app_capture_take_report(void);

#endif
