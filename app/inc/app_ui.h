/**
 * @file app_ui.h
 * @brief LCD 菜单状态机接口。
 */
#ifndef APP_UI_H
#define APP_UI_H

#include "app_keys.h"

#include <stdint.h>

typedef enum {
    APP_UI_PAGE_DASHBOARD = 0, /**< 主仪表盘页。 */
    APP_UI_PAGE_TEST_MENU,   /**< 顶层测试菜单页。 */
    APP_UI_PAGE_PWM,         /**< PWM 频率设置页。 */
    APP_UI_PAGE_MEASURE,     /**< 频率测量和串口上报设置页。 */
    APP_UI_PAGE_DA,          /**< DAC 波形监看页。 */
    APP_UI_PAGE_INFO         /**< 系统信息页。 */
} app_ui_page_t;

/**
 * @brief LCD 菜单状态。
 */
typedef struct {
    app_ui_page_t page;          /**< 当前所在页面。 */
    uint8_t cursor;              /**< 当前页内光标位置。 */
    uint8_t editing;             /**< 是否正在编辑数值（当前仅用于 PWM）。 */
    uint8_t serial_auto_report_enabled; /**< 串口自动上报开关显示值。 */
    uint32_t pwm_edit_frequency_hz;     /**< 编辑中的 PWM 频率草稿。 */
} app_ui_state_t;

/**
 * @brief 初始化菜单状态。
 */
void app_ui_init(void);

/**
 * @brief 按键事件输入菜单状态机。
 *
 * @return 1 表示页面内容变化，主循环需刷新 LCD。
 */
uint8_t app_ui_handle_key(app_key_event_t key);

/**
 * @brief 获取当前菜单状态（只读）。
 */
const app_ui_state_t *app_ui_get_state(void);

#endif
