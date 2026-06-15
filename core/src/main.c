/**
 * @file main.c
 * @brief 固件主入口。
 *
 * 复位后由启动文件跳转到 main，先初始化基础延时能力，再进入应用层初始化
 * 和主循环。业务代码放在 app 模块，main 只保留最小启动流程。
 */
#include "app.h"
#include "delay.h"
#include "stm32f10x.h"

int main(void)
{
    /*
     * delay_init 需要尽早调用，因为 LCD 初始化和很多外设上电等待都要用到
     * delay_ms/delay_us。
     */
    delay_init();

    /* 进入应用层初始化；main 不展开每个模块的启动细节。 */
    app_init();

    /* app_run 是永不返回的主循环。 */
    app_run();

    while (1) {
    }
}
