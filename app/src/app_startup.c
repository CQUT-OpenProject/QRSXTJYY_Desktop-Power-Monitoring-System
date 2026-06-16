#include "app_startup.h"

#include "app_capture.h"
#include "app_command.h"
#include "app_dac.h"
#include "app_display.h"
#include "app_keys.h"
#include "app_protocol.h"
#include "app_pwm.h"
#include "app_ui.h"
#include "board.h"

/**
 * @brief 按安全顺序初始化板级资源和应用模块。
 */
void app_startup_init(void)
{
    // 配置需要安全电平的 GPIO
    board_init();

    // 初始化 LCD
    app_display_init();

    // 初始化按键
    app_keys_init();

    // 初始化通信协议
    app_protocol_init();
    app_pwm_init();
    app_capture_init();
    app_dac_init();
    app_command_init();
    app_ui_init();
}
