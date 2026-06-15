/**
 * @file app_display.c
 * @brief LCD 显示应用模块。
 *
 * 当前用于验证 ALIENTEK TFT LCD 驱动、基础延时以及 C 主工程调用 Rust
 * 静态库的链路。后续可在本模块中替换为正式的用电监控界面。
 */
#include "app_display.h"

#include "delay.h"
#include "lcd.h"
#include "rust_algos.h"

void app_display_init(void)
{
    uint32_t rust_ver = pm_algos_version();
    uint32_t p_x100 = pm_calc_power_x100(22000U, 1000U);

    LCD_Init();

    LCD_Clear(RED);
    delay_ms(250U);
    LCD_Clear(GREEN);
    delay_ms(250U);
    LCD_Clear(BLUE);
    delay_ms(250U);

    LCD_Clear(BLACK);
    LCD_Fill(0, 0, lcddev.width - 1, 19, RED);
    LCD_Fill(0, 20, lcddev.width - 1, 39, GREEN);
    LCD_Fill(0, 40, lcddev.width - 1, 59, BLUE);

    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
    LCD_ShowString(20, 80, 220, 24, 24, (u8 *)"MiniSTM32 RCT6");

    POINT_COLOR = YELLOW;
    LCD_ShowString(20, 145, 220, 16, 16, (u8 *)"LCD init OK");

    POINT_COLOR = CYAN;
    if (rust_ver == 0x00010000U && p_x100 == 22000U) {
        LCD_ShowString(20, 170, 220, 16, 16, (u8 *)"Rust link OK");
    } else {
        LCD_ShowString(20, 170, 220, 16, 16, (u8 *)"Rust link ERR");
    }
}

void app_display_task(void)
{
}
