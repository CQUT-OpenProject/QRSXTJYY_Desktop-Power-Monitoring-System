/**
 * @file app_display.h
 * @brief LCD 显示应用模块接口。
 */
#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "app_monitor_state.h"
#include "app_ui.h"

/**
 * @brief 初始化 LCD 显示内容。
 */
void app_display_init(void);

/**
 * @brief 执行显示模块周期任务。
 *
 * LCD 不适合在主循环里一直刷；这里按刷新请求或固定间隔更新画面，
 * 减少闪烁，也少占一点总线时间。
 */
void app_display_task(const app_monitor_state_t *state, const app_ui_state_t *ui);

/**
 * @brief 标记显示内容需要刷新。
 */
void app_display_request_refresh(void);

#endif
