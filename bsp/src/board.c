#include "board.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/**
 * @brief 初始化板级默认 GPIO 电平和片选状态。
 */
void board_init(void)
{
    // 板级片选引脚的 GPIO 输出配置
    GPIO_InitTypeDef gpio;

    // PC13 连接 TFTLCD 触摸控制器片选。项目不使用触摸屏，将其拉高。
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_13;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);

    // PA2/PA3 分别连接板载 W25Q64 和 SD 卡片选。
    gpio.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_SetBits(GPIOA, GPIO_Pin_2 | GPIO_Pin_3);
}
