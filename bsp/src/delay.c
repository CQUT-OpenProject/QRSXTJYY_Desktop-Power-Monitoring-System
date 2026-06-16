/**
 * @file delay.c
 * @brief 基于 SysTick 的阻塞式延时实现。
 *
 * LCD 初始化和部分外设时序需要微秒/毫秒级阻塞延时。这里使用 STM32F1
 * 标准库配置 SysTick 为 HCLK/8 时钟源，提供裸机环境下可直接使用的
 * delay_us 和 delay_ms。
 */
#include "delay.h"

#include "misc.h"
#include "stm32f10x.h"

// SysTick 在 1 微秒内需要计数的 tick 数
static uint32_t s_ticks_per_us;
// SysTick 在 1 毫秒内需要计数的 tick 数
static uint32_t s_ticks_per_ms;

/**
 * @brief 初始化 SysTick 延时计数基准。
 */
void delay_init(void)
{
    // SysTick 时钟源为 HCLK/8
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);

    // 计算每微秒和每毫秒的 SysTick 计数值
    s_ticks_per_us = SystemCoreClock / 8000000U;
    s_ticks_per_ms = s_ticks_per_us * 1000U;
}

/**
 * @brief 阻塞延时指定微秒数。
 */
void delay_us(uint32_t us)
{
    // SysTick 控制寄存器快照，用来检查 COUNTFLAG
    uint32_t ctrl;

    SysTick->LOAD = us * s_ticks_per_us;
    // 清零计数器
    SysTick->VAL = 0x00U;
    // 启动 SysTick
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

    // 轮询 SysTick->CTRL 里的 COUNTFLAG 位
    // 等 SysTick 从 LOAD 倒数到 0，COUNTFLAG 置位，延时结束。
    do {
        ctrl = SysTick->CTRL;
    } while ((ctrl & SysTick_CTRL_ENABLE_Msk) && !(ctrl & SysTick_CTRL_COUNTFLAG_Msk));

    // 关闭 SysTick
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->VAL = 0x00U;
}

/**
 * @brief 阻塞延时指定毫秒数。
 */
void delay_ms(uint32_t ms)
{
    // SysTick 控制寄存器快照，用来检查 COUNTFLAG
    uint32_t ctrl;

    while (ms > 0U) {
        // SysTick 的 LOAD 是 24 位，最大只能装：0xFFFFFF = 16777215
        // 72 MHz / 8 下，1 ms 是 9000 tick，所以一次最多大约：
        // 16777215 / 9000 ≈ 1864，这里取 1800
        // 本次延时块的毫秒数，避免超过 SysTick 24 位 LOAD 上限
        uint32_t chunk = ms > 1800U ? 1800U : ms;

        SysTick->LOAD = chunk * s_ticks_per_ms;
        SysTick->VAL = 0x00U;
        SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

        do {
            ctrl = SysTick->CTRL;
        } while ((ctrl & SysTick_CTRL_ENABLE_Msk) && !(ctrl & SysTick_CTRL_COUNTFLAG_Msk));

        SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
        SysTick->VAL = 0x00U;

        ms -= chunk;
    }
}
