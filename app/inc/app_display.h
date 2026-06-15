/**
 * @file app_display.h
 * @brief LCD 显示应用模块接口。
 */
#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

/**
 * @brief 初始化 LCD 显示内容。
 */
void app_display_init(void);

/**
 * @brief 执行显示模块周期任务。
 */
void app_display_task(void);

#endif
