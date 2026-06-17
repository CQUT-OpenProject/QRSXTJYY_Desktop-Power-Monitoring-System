#include "app_keys.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define APP_KEYS_DEBOUNCE_US 20000U

#define APP_KEY_RAW_UP      0x01U
#define APP_KEY_RAW_DOWN    0x02U
#define APP_KEY_RAW_CONFIRM 0x04U

static uint8_t s_last_raw;
static uint8_t s_stable_raw;
static uint32_t s_last_change_us;

/** 读取三个按键的原始按下位图。 */
static uint8_t read_raw_keys(void)
{
    uint8_t raw = 0U;

    /* KEY1(PA15)/KEY0(PC5) 低有效；KEYUP(PA0) 高有效 */
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == Bit_RESET) {
        raw |= APP_KEY_RAW_UP;
    }
    if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5) == Bit_RESET) {
        raw |= APP_KEY_RAW_DOWN;
    }
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET) {
        raw |= APP_KEY_RAW_CONFIRM;
    }

    return raw;
}

/** 将原始位图转换为单个 UI 事件，多键同时按下时取优先级最高的。 */
static app_key_event_t raw_to_event(uint8_t raw)
{
    if ((raw & APP_KEY_RAW_UP) != 0U) {
        return APP_KEY_EVENT_UP;
    }
    if ((raw & APP_KEY_RAW_DOWN) != 0U) {
        return APP_KEY_EVENT_DOWN;
    }
    if ((raw & APP_KEY_RAW_CONFIRM) != 0U) {
        return APP_KEY_EVENT_CONFIRM;
    }
    return APP_KEY_EVENT_NONE;
}

/**
 * @brief 初始化 KEY1(PA15)、KEY0(PC5)、KEYUP(PA0) 的 GPIO 输入。
 *
 * PA15 默认是 JTAG 引脚，使用前需关闭 JTAG 保留 SWD。
 */
void app_keys_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &gpio);

    s_last_raw = read_raw_keys();
    s_stable_raw = s_last_raw;
    s_last_change_us = 0U;
}

/**
 * @brief 扫描按键并返回消抖后的事件。
 *
 * 电平变化后等待 20 ms 稳定窗口，稳定值变化时才产生事件。
 */
app_key_event_t app_keys_poll(uint32_t now_us)
{
    uint8_t raw = read_raw_keys();

    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_last_change_us = now_us;
        return APP_KEY_EVENT_NONE;
    }

    if ((uint32_t)(now_us - s_last_change_us) < APP_KEYS_DEBOUNCE_US) {
        return APP_KEY_EVENT_NONE;
    }

    if (raw == s_stable_raw) {
        return APP_KEY_EVENT_NONE;
    }

    s_stable_raw = raw;
    return raw_to_event(s_stable_raw);
}
