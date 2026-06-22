#include "app_capture.h"

#include "misc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define APP_CAPTURE_TIMER_CLOCK_HZ 72000000U
#define APP_CAPTURE_COUNTER_HZ 1000000U
#define APP_CAPTURE_TIMEOUT_US 2000000U
#define APP_CAPTURE_REPORT_US 1000000U
#define APP_CAPTURE_CHANGE_REPORT_MIN_US 200000U

/*
 * 数据所有权：
 * - TIM2 ISR 写 s_overflows / s_frequency_x100 / s_last_capture_us / s_has_capture；
 * - 主循环 app_capture_task() / app_capture_get_time_us() 读上述变量，读 s_overflows
 *   与 TIM2->CNT 时关中断避免溢出高位与低位错位；
 * - s_report_pending / s_report_freq_x100 由主循环读写，ISR 不访问。
 */
static volatile uint32_t s_overflows;
static volatile uint32_t s_last_capture_us;
static volatile uint32_t s_frequency_x100;
static volatile uint8_t  s_has_capture;

static uint32_t s_last_report_us;
static uint32_t s_last_reported_freq_x100 = 0xFFFFFFFFU;
static uint32_t s_report_freq_x100;
static uint8_t  s_report_pending;

/** 将 TIM2 的 16 位捕获值扩展为 32 位微秒时间。 */
static uint32_t make_capture_time(uint16_t captured)
{
    uint32_t high = s_overflows;

    /*
     * 捕获与溢出可能几乎同时发生。若硬件已置位 Update 标志且捕获值很小，
     * 说明捕获在溢出之后，高位需补加 1。
     */
    if ((TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) && captured < 32768U) {
        high++;
    }

    return (high << 16) | captured;
}

/**
 * @brief 初始化 PA1/TIM2_CH2 输入捕获测频模块。
 *
 * TIM2 以 1 MHz 运行（72 MHz / 72），计数器每加 1 = 1 μs。
 * 相邻上升沿捕获值相减即为信号周期（μs），频率 = 1,000,000 / 周期。
 * 16 位计数器溢出中断扩展时间高位为 32 位。
 */
void app_capture_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time_base;
    TIM_ICInitTypeDef ic;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_Period = 0xFFFFU;
    time_base.TIM_Prescaler = (uint16_t)((APP_CAPTURE_TIMER_CLOCK_HZ / APP_CAPTURE_COUNTER_HZ) - 1U);
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &time_base);

    TIM_ICStructInit(&ic);
    ic.TIM_Channel = TIM_Channel_2;
    ic.TIM_ICPolarity = TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter = 0x03U;
    TIM_ICInit(TIM2, &ic);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update | TIM_IT_CC2);
    TIM_ITConfig(TIM2, TIM_IT_Update | TIM_IT_CC2, ENABLE);

    nvic.NVIC_IRQChannel = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 1U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    s_overflows = 0U;
    s_last_capture_us = 0U;
    s_frequency_x100 = 0U;
    s_has_capture = 0U;
    s_last_report_us = 0U;
    s_report_pending = 0U;

    TIM_Cmd(TIM2, ENABLE);
}

/**
 * @brief 主循环中处理超时和无信号检测 + 自动上报节流。
 *
 * - 超过 2 秒无边沿 → 频率清零（LCD/串口不显示旧值）；
 * - 周期上报每 1 秒一次；变化上报间隔 ≥ 200 ms 防抖。
 */
void app_capture_task(void)
{
    uint32_t now = app_capture_get_time_us();
    uint32_t freq = s_frequency_x100;
    uint8_t period_due;
    uint8_t changed_due;

    if ((s_has_capture != 0U) && ((now - s_last_capture_us) > APP_CAPTURE_TIMEOUT_US)) {
        s_frequency_x100 = 0U;
        s_has_capture = 0U;
        freq = 0U;
    }

    period_due = (uint8_t)((now - s_last_report_us) >= APP_CAPTURE_REPORT_US);
    changed_due = (uint8_t)((freq != s_last_reported_freq_x100) &&
                            ((now - s_last_report_us) >= APP_CAPTURE_CHANGE_REPORT_MIN_US));

    if (period_due != 0U || changed_due != 0U) {
        s_report_freq_x100 = freq;
        s_report_pending = 1U;
        s_last_reported_freq_x100 = freq;
        s_last_report_us = now;
    }
}

/**
 * @brief TIM2 中断：捕获上升沿 + 溢出计数扩展。
 *
 * 捕获：相邻上升沿时间差 → 周期 (μs) → 频率 Hz×100。
 * 溢出：16 位计数器每溢出一次，s_overflows 加 1。
 */
void app_capture_tim2_irq_handler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_CC2) != RESET) {
        static uint32_t previous_capture_us;
        uint16_t captured = TIM_GetCapture2(TIM2);
        uint32_t current_capture_us = make_capture_time(captured);

        TIM_ClearITPendingBit(TIM2, TIM_IT_CC2);

        if (s_has_capture != 0U) {
            uint32_t period_us = current_capture_us - previous_capture_us;
            if (period_us > 0U) {
                /* Hz × 100 = 100,000,000 / period_us */
                s_frequency_x100 = (uint32_t)(100000000ULL / period_us);
            }
        }

        previous_capture_us = current_capture_us;
        s_last_capture_us = current_capture_us;
        s_has_capture = 1U;
    }

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        s_overflows++;
    }
}

uint32_t app_capture_get_frequency_x100(void)
{
    return s_frequency_x100;
}

/**
 * @brief 获取 32 位微秒时间戳：s_overflows(高 16 位) + TIM2->CNT(低 16 位)。
 *
 * 关中断读高低位，避免溢出期间读到不一致的值。
 */
uint32_t app_capture_get_time_us(void)
{
    uint32_t high;
    uint16_t low;

    __disable_irq();
    high = s_overflows;
    low = TIM_GetCounter(TIM2);
    if ((TIM_GetFlagStatus(TIM2, TIM_FLAG_Update) != RESET) && low < 32768U) {
        high++;
    }
    __enable_irq();

    return (high << 16) | low;
}

uint8_t app_capture_take_report(void)
{
    if (s_report_pending == 0U) {
        return 0U;
    }

    s_report_pending = 0U;
    return 1U;
}
