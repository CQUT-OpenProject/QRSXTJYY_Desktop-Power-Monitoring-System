/**
 * @file app.c
 * @brief 应用层总入口。
 *
 * 负责按顺序初始化各应用模块，并进入应用主循环。外设细节由各子模块
 * 或 BSP 层封装，避免 main 直接依赖具体业务模块。
 */
#include "app.h"

#include "app_adc.h"
#include "app_capture.h"
#include "app_dac.h"
#include "app_display.h"
#include "app_protocol.h"
#include "app_pwm.h"
#include "board.h"

void app_init(void)
{
    board_init();

    app_adc_init();
    app_dac_init();
    app_pwm_init();
    app_capture_init();
    app_protocol_init();

    app_display_init();
}

void app_run(void)
{
    app_display_task();

    while (1) {
    }
}
