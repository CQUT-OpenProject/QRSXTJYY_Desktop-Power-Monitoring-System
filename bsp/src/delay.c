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

static uint32_t s_ticks_per_us;
static uint32_t s_ticks_per_ms;

void delay_init(void)
{
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
    s_ticks_per_us = SystemCoreClock / 8000000U;
    s_ticks_per_ms = s_ticks_per_us * 1000U;
}

void delay_us(uint32_t us)
{
    uint32_t ctrl;

    SysTick->LOAD = us * s_ticks_per_us;
    SysTick->VAL = 0x00U;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

    do {
        ctrl = SysTick->CTRL;
    } while ((ctrl & SysTick_CTRL_ENABLE_Msk) && !(ctrl & SysTick_CTRL_COUNTFLAG_Msk));

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->VAL = 0x00U;
}

void delay_ms(uint32_t ms)
{
    uint32_t ctrl;

    while (ms > 0U) {
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
