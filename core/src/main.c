/**
 * @file main.c
 * @brief 固件主入口。
 *
 * 复位后由启动文件跳转到 main，先初始化基础延时能力，再进入应用层初始化
 * 和主循环。具体业务逻辑放在 app 模块中，main 只保留最小启动流程。
 */
#include "app.h"
#include "delay.h"
#include "stm32f10x.h"

int main(void)
{
    delay_init();

    app_init();
    app_run();

    while (1) {
    }
}
