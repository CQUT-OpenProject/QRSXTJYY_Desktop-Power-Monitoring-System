#include "app_pwm.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define APP_PWM_TIMER_CLOCK_HZ 72000000U
#define APP_PWM_DEFAULT_HZ 50U
#define APP_PWM_MIN_HZ 1U
#define APP_PWM_MAX_HZ 100000U

/* 保存用户当前设置的频率，供 LCD、串口 STATUS 和 UI 编辑页读取。 */
static uint32_t s_pwm_frequency_hz = APP_PWM_DEFAULT_HZ;

static uint32_t clamp_frequency(uint32_t hz)
{
    /* 频率太低或太高都可能让定时器参数不可用，所以先限制范围。 */
    if (hz < APP_PWM_MIN_HZ) {
        return APP_PWM_MIN_HZ;
    }
    if (hz > APP_PWM_MAX_HZ) {
        return APP_PWM_MAX_HZ;
    }
    return hz;
}

static void calc_timer_params(uint32_t hz, uint16_t *psc, uint16_t *arr)
{
    /*
     * TIM1 输入时钟为 72 MHz。PWM 频率由 prescaler 和 period 共同决定：
     * PWM = 72MHz / (PSC + 1) / (ARR + 1)。
     */
    uint32_t target_ticks = (APP_PWM_TIMER_CLOCK_HZ + (hz / 2U)) / hz;
    uint32_t prescaler;
    uint32_t period;

    if (target_ticks < 2U) {
        target_ticks = 2U;
    }

    /*
     * ARR 是 16 位，最大只能表示 65535。先估算需要多大的预分频，
     * 再反算 ARR，保证两个寄存器都不越界。
     */
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

void app_pwm_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time_base;
    TIM_OCInitTypeDef oc;

    /* PA8 是 TIM1_CH1 的复用输出脚，同时打开 GPIOA 和 TIM1 时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_StructInit(&gpio);
    /* 复用推挽输出表示引脚由 TIM1 外设驱动，而不是普通 GPIO 手动翻转。 */
    gpio.GPIO_Pin = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM1, &time_base);

    TIM_OCStructInit(&oc);
    /*
     * PWM1 模式下，CNT 小于 CCR 时输出有效电平。后面把 CCR 设置成周期一半，
     * 就能得到 50% 占空比方波。
     */
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &oc);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);

    /* 先写入默认频率，再启动 TIM1。 */
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

    /* ARR 是“最大计数值”，实际周期计数点数要加 1。 */
    period = (uint32_t)arr + 1U;
    TIM_PrescalerConfig(TIM1, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(TIM1, arr);
    /* CCR1 取周期一半，就能得到 50% 占空比。 */
    TIM_SetCompare1(TIM1, (uint16_t)(period / 2U));
    TIM_GenerateEvent(TIM1, TIM_EventSource_Update);

    s_pwm_frequency_hz = hz;
    return s_pwm_frequency_hz;
}

uint32_t app_pwm_get_frequency(void)
{
    return s_pwm_frequency_hz;
}
