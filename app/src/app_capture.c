#include "app_capture.h"

#include "misc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define APP_CAPTURE_TIMER_CLOCK_HZ 72000000U
#define APP_CAPTURE_COUNTER_HZ 1000000U
#define APP_CAPTURE_TIMEOUT_US 2000000U
#define APP_CAPTURE_REPORT_US 1000000U

/*
 * TIM2 是 16 位计数器，硬件计数到 0xFFFF 后会回到 0。
 * s_overflows 记录溢出次数，用来拼出一个 32 位“微秒时间”。
 */
static volatile uint32_t s_overflows;

/* 最近一次捕获到上升沿的时间，用于判断输入信号是否超时消失。 */
static volatile uint32_t s_last_capture_us;

/* 频率保存为 Hz * 100，例如 50.00 Hz 保存为 5000，避免使用浮点数。 */
static volatile uint32_t s_frequency_x100;

/* 第一次捕获只能作为起点，第二次捕获后才有周期和频率。 */
static volatile uint8_t s_has_capture;

/* 串口自动上报用这几个变量节流，避免每轮主循环都刷屏。 */
static uint32_t s_last_report_us;
static uint32_t s_last_reported_freq_x100 = 0xFFFFFFFFU;
static uint32_t s_report_freq_x100;
static uint8_t s_report_pending;

static uint32_t make_capture_time(uint16_t captured)
{
    uint32_t high = s_overflows;

    /*
     * 捕获中断和溢出中断可能几乎同时发生。若硬件已经置位 Update 标志，
     * 且捕获值很小，说明捕获发生在溢出后，需要把高位补加 1。
     */
    if ((TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) && captured < 32768U) {
        high++;
    }

    return (high << 16) | captured;
}

void app_capture_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time_base;
    TIM_ICInitTypeDef ic;
    NVIC_InitTypeDef nvic;

    /* PA1 接入 TIM2_CH2，先打开 GPIOA 和 TIM2 的时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_StructInit(&gpio);
    /* PA1 作为外部频率输入脚，外部电路负责提供有效电平。 */
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    /*
     * TIM2 计数频率设为 1 MHz，计数器每加 1 就是 1 微秒。
     * 后面两个捕获值一减，就是输入信号周期。
     */
    time_base.TIM_Period = 0xFFFFU;
    time_base.TIM_Prescaler = (uint16_t)((APP_CAPTURE_TIMER_CLOCK_HZ / APP_CAPTURE_COUNTER_HZ) - 1U);
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &time_base);

    TIM_ICStructInit(&ic);
    /*
     * CH2 捕获上升沿。两个相邻上升沿的时间差就是输入信号周期。
     * ICFilter 做一点硬件滤波，降低毛刺触发概率。
     */
    ic.TIM_Channel = TIM_Channel_2;
    ic.TIM_ICPolarity = TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter = 0x03U;
    TIM_ICInit(TIM2, &ic);

    /* 同时打开捕获中断和溢出中断：一个记录边沿，一个扩展时间高位。 */
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

void app_capture_task(void)
{
    uint32_t now = app_capture_get_time_us();
    uint32_t freq = s_frequency_x100;

    /*
     * 超过 2 秒没有新边沿，就认为输入频率为 0。
     * 拔掉输入线后，LCD 和串口不会一直显示旧频率。
     */
    if ((s_has_capture != 0U) && ((now - s_last_capture_us) > APP_CAPTURE_TIMEOUT_US)) {
        s_frequency_x100 = 0U;
        s_has_capture = 0U;
        freq = 0U;
    }

    /*
     * 频率有变化，或者到达固定上报周期时，才准备一条上报。
     * app_run 之后会调用 app_capture_take_report 取走它。
     */
    if (((now - s_last_report_us) >= APP_CAPTURE_REPORT_US) ||
        (freq != s_last_reported_freq_x100 && s_last_report_us == 0U)) {
        if (freq != s_last_reported_freq_x100) {
            s_report_freq_x100 = freq;
            s_report_pending = 1U;
            s_last_reported_freq_x100 = freq;
        }
        s_last_report_us = now;
    }
}

void app_capture_tim2_irq_handler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_CC2) != RESET) {
        static uint32_t previous_capture_us;
        uint16_t captured = TIM_GetCapture2(TIM2);
        uint32_t current_capture_us = make_capture_time(captured);

        TIM_ClearITPendingBit(TIM2, TIM_IT_CC2);

        /*
     * 第二个以及后面的上升沿才能计算周期。period_us 是两个上升沿间隔，
         * 频率 Hz = 1,000,000 / period_us；为了保留两位小数再乘 100。
         */
        if (s_has_capture != 0U) {
            uint32_t period_us = current_capture_us - previous_capture_us;
            if (period_us > 0U) {
                s_frequency_x100 = (uint32_t)(100000000ULL / period_us);
            }
        }

        previous_capture_us = current_capture_us;
        s_last_capture_us = current_capture_us;
        s_has_capture = 1U;
    }

    /* 计数器溢出一次，高 16 位加 1。 */
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        s_overflows++;
    }
}

uint32_t app_capture_get_frequency_x100(void)
{
    return s_frequency_x100;
}

uint32_t app_capture_get_time_us(void)
{
    uint32_t high;
    uint16_t low;

    /*
     * 读取 s_overflows 和 TIM2->CNT 时短暂关中断，避免读高位和低位之间
     * 刚好发生溢出，拼出不一致的时间。
     */
    __disable_irq();
    high = s_overflows;
    low = TIM_GetCounter(TIM2);
    if ((TIM_GetFlagStatus(TIM2, TIM_FLAG_Update) != RESET) && low < 32768U) {
        high++;
    }
    __enable_irq();

    return (high << 16) | low;
}

uint8_t app_capture_take_report(uint32_t *freq_x100)
{
    /* pending 标志清掉后，同一条上报就不会被重复发送。 */
    if (s_report_pending == 0U) {
        return 0U;
    }

    *freq_x100 = s_report_freq_x100;
    s_report_pending = 0U;
    return 1U;
}
