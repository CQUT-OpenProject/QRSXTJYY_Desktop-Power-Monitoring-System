#include "board.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

void board_init(void)
{
    GPIO_InitTypeDef gpio;

    /*
     * PC13 连接 TFTLCD 触摸控制器片选。项目不使用触摸屏时，先把它拉高，
     * 防止触摸控制器误被选中影响 LCD 或其它共享线。
     */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_13;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);
}
