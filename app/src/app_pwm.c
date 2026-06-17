#include "app_pwm.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define APP_PWM_TIMER_CLOCK_HZ 72000000U
#define APP_PWM_DEFAULT_HZ 50U
#define APP_PWM_MIN_HZ 1U
#define APP_PWM_MAX_HZ 100000U

static uint32_t s_pwm_frequency_hz = APP_PWM_DEFAULT_HZ;

/** 将频率限制到定时器可输出范围。 */
static uint32_t clamp_frequency(uint32_t hz)
{
    if (hz < APP_PWM_MIN_HZ) {
        return APP_PWM_MIN_HZ;
    }
    if (hz > APP_PWM_MAX_HZ) {
        return APP_PWM_MAX_HZ;
    }
    return hz;
}

/**
 * @brief 根据目标频率计算 TIM1 的 PSC 和 ARR。
 *
 * PWM = 72 MHz / (PSC + 1) / (ARR + 1)。
 * ARR 为 16 位最大值 65535，先估算 PSC 再反算 ARR 保证两者不越界。
 */
static void calc_timer_params(uint32_t hz, uint16_t *psc, uint16_t *arr)
{
    uint32_t target_ticks = (APP_PWM_TIMER_CLOCK_HZ + (hz / 2U)) / hz;
    uint32_t prescaler;
    uint32_t period;

    if (target_ticks < 2U) {
        target_ticks = 2U;
    }

    prescaler = (target_ticks + 65535U) / 65536U;
    if (prescaler > 0U) {
        prescaler--;
    }
    if (prescaler > 65535U) {
        prescaler = 65535U;
    }

    period = ((APP_PWM_TIMER_CLOCK_HZ / (prescaler + 1U)) + (hz / 2U)) / hz;
    if (period < 2U) {
        period = 2U;
    }
    if (period > 65536U) {
        period = 65536U;
    }

    *psc = (uint16_t)prescaler;
    *arr = (uint16_t)(period - 1U);
}

/**
 * @brief 初始化 PA8/TIM1_CH1 的 50% 占空比 PWM 输出。
 */
void app_pwm_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time_base;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM1, &time_base);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &oc);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);

    app_pwm_set_frequency(APP_PWM_DEFAULT_HZ);
    TIM_Cmd(TIM1, ENABLE);
}

uint32_t app_pwm_set_frequency(uint32_t hz)
{
    uint16_t psc;
    uint16_t arr;
    uint32_t period;

    hz = clamp_frequency(hz);
    calc_timer_params(hz, &psc, &arr);

    period = (uint32_t)arr + 1U;
    TIM_PrescalerConfig(TIM1, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(TIM1, arr);
    TIM_SetCompare1(TIM1, (uint16_t)(period / 2U));
    TIM_GenerateEvent(TIM1, TIM_EventSource_Update);

    s_pwm_frequency_hz = hz;
    return s_pwm_frequency_hz;
}

uint32_t app_pwm_get_frequency(void)
{
    return s_pwm_frequency_hz;
}
