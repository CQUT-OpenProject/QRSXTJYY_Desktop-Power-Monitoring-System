#include "app_keys.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define APP_KEYS_DEBOUNCE_US 20000U

#define APP_KEY_RAW_UP 0x01U
#define APP_KEY_RAW_DOWN 0x02U
#define APP_KEY_RAW_CONFIRM 0x04U

/* 上一次读到的原始电平组合，用于判断电平是否刚发生变化。 */
static uint8_t s_last_raw;

/* 已经通过消抖确认的稳定电平组合。 */
static uint8_t s_stable_raw;

/* 原始电平最后一次变化的时间，用来等待 20ms 消抖窗口。 */
static uint32_t s_last_change_us;

static uint8_t read_raw_keys(void)
{
    uint8_t raw = 0U;

    /*
     * KEY1/KEY0 是低有效，按下时 GPIO 读到 RESET。
     * KEYUP 是高有效，按下时 GPIO 读到 SET。
     */
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

static app_key_event_t raw_to_event(uint8_t raw)
{
    /*
     * 如果多个键同时按下，只返回优先级最高的一个事件。
     * 这个菜单很简单，不需要组合键。
     */
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

void app_keys_init(void)
{
    GPIO_InitTypeDef gpio;

    /* PA15 默认是 JTAG 引脚，用 KEY1 前要关掉 JTAG，只保留 SWD 下载调试。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    GPIO_StructInit(&gpio);
    /* KEY1 接 PA15，低有效，所以使用上拉输入。 */
    gpio.GPIO_Pin = GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    /* KEY0 接 PC5，同样是低有效。 */
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gpio);

    /* KEYUP 接 PA0，高有效，所以使用下拉输入。 */
    gpio.GPIO_Pin = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &gpio);

    s_last_raw = read_raw_keys();
    s_stable_raw = s_last_raw;
    s_last_change_us = 0U;
}

app_key_event_t app_keys_poll(uint32_t now_us)
{
    uint8_t raw = read_raw_keys();

    /*
     * 原始电平变化时先记时间，不马上返回事件。
     * 机械按键按下和松开的一小段抖动会被这一步过滤掉。
     */
    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_last_change_us = now_us;
        return APP_KEY_EVENT_NONE;
    }

    /* 变化后不足 20ms，继续等待稳定。 */
    if ((uint32_t)(now_us - s_last_change_us) < APP_KEYS_DEBOUNCE_US) {
        return APP_KEY_EVENT_NONE;
    }

    /* 稳定值没有变化，就没有新的按键事件。 */
    if (raw == s_stable_raw) {
        return APP_KEY_EVENT_NONE;
    }

    s_stable_raw = raw;
    return raw_to_event(s_stable_raw);
}
