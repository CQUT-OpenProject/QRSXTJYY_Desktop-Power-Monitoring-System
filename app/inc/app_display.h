/**
 * @file app_display.h
 * @brief LCD 显示模块接口。
 */
#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "app_monitor_state.h"
#include "app_ui.h"

/**
 * @brief 初始化 LCD 并显示固定提示栏。
 */
void app_display_init(void);

/**
 * @brief 显示周期任务，在主循环中调用。
 *
 * 有刷新请求或距上次刷新超过 500 ms 时重绘当前页面。
 *
 * @param state 监控状态快照。
 * @param ui    UI 菜单状态。
 */
void app_display_task(const app_monitor_state_t *state, const app_ui_state_t *ui);

/**
 * @brief 请求下一次显示任务重绘页面。
 */
void app_display_request_refresh(void);

#endif
