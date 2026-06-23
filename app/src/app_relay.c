/**
 * @file app_relay.c
 * @brief miniIO 副板继电器控制实现。
 *
 * 硬件映射：
 *   - 控制引脚：GPIOC GPIO_Pin_12（PC12）
 *   - 有效电平：高电平（GPIO_SetBits）→ 继电器吸合
 *   - 无效电平：低电平（GPIO_ResetBits）→ 继电器释放
 *
 * PC12 在 MiniSTM32 板上标注为 IIC_SCL（连接 24C02），但本工程未使用
 * EEPROM/IIC，可安全复用为通用推挽输出。
 *
 * 注意：board.c 中已对 RCC_APB2Periph_GPIOC 执行过 ClockCmd，
 * 此处重复调用无害（标准库对同一时钟的多次 ENABLE 是幂等的）。
 */
#include "app_relay.h"

#include "stm32f10x.h"

static uint8_t s_relay_on = 0U;

void app_relay_init(void)
{
    GPIO_InitTypeDef gpio;

    /* 使能 GPIOC 时钟（board.c 已经开启，此处重复调用幂等，保持模块自洽）*/
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = GPIO_Pin_12;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);

    /* 上电先关闭继电器，避免初始化瞬间误动作；
     * UI 完成初始化并进入 Dashboard 后由状态机打开。 */
    app_relay_set(0U);
}

void app_relay_set(uint8_t on)
{
    s_relay_on = (on != 0U) ? 1U : 0U;

    if (s_relay_on != 0U) {
        GPIO_SetBits(GPIOC, GPIO_Pin_12);
    } else {
        GPIO_ResetBits(GPIOC, GPIO_Pin_12);
    }
}

uint8_t app_relay_get(void)
{
    return s_relay_on;
}
