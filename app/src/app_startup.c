#include "app_startup.h"

#include "app_adc.h"
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
 *
 * 顺序说明：
 * 1. board_init → 需要安全电平的先配置；
 * 2. 外设初始化和应用层之间的依赖次序由各 init 内部保证。
 */
void app_startup_init(void)
{
    board_init();
    app_display_init();
    app_keys_init();
    app_protocol_init();
    app_pwm_init();
    app_capture_init();
    app_dac_init();
    app_adc_init();
    app_command_init();
    app_ui_init();
}
