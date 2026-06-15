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

void app_startup_init(void)
{
    /*
     * board_init 放最前面，先把开发板上需要安全电平的 GPIO 设好。
     */
    board_init();

    /*
     * LCD 初始化比较慢，还会操作总线和延时。
     * 先把 LCD 准备好，再开串口和定时器中断。
     */
    app_display_init();

    /* 按键只依赖 GPIO，放在串口、定时器启动前初始化。 */
    app_keys_init();

    /*
     * 下面这些模块会启用 USART/TIM/DAC/DMA。先启动通信和测试源，
     * 再启动测量与波形输出，最后初始化只保存状态的模块。
     */
    app_protocol_init();
    app_pwm_init();
    app_capture_init();
    app_dac_init();
    app_command_init();
    app_ui_init();
}
