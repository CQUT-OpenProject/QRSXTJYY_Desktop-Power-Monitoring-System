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

/**
 * @brief 固件入口，初始化基础延时和应用层后进入主循环。
 */
int main(void)
{
    delay_init();
    app_init();
    app_run();

    while (1) {}
}
