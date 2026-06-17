/**
 * @file app_ui.h
 * @brief LCD menu state machine interface.
 */
#ifndef APP_UI_H
#define APP_UI_H

#include "app_keys.h"

#include <stdint.h>

typedef enum {
    /** 顶层菜单页。 */
    APP_UI_PAGE_MENU = 0,

    /** PWM 频率设置页。 */
    APP_UI_PAGE_PWM,

    /** 频率测量和串口上报设置页。 */
    APP_UI_PAGE_MEASURE,

    /** DA 波形监看页。 */
    APP_UI_PAGE_DA,

    /** AC 电参数测量页。 */
    APP_UI_PAGE_ADC
} app_ui_page_t;

/**
 * @brief LCD 菜单状态。
 */
typedef struct {
    /** 当前所在页面。 */
    app_ui_page_t page;

    /** 当前页面内光标选中的项目序号。 */
    uint8_t cursor;

    /** 是否正在编辑数值，当前只用于 PWM 频率。 */
    uint8_t editing;

    /** 串口自动上报开关，用于显示当前状态。 */
    uint8_t serial_auto_report_enabled;

    /** 正在编辑的 PWM 频率草稿，确认后才写入 PWM 模块。 */
    uint32_t pwm_edit_frequency_hz;
} app_ui_state_t;

/**
 * @brief 初始化菜单状态。
 */
void app_ui_init(void);

/**
 * @brief 把一个按键事件喂给菜单状态机。
 *
 * @return 1 表示页面内容变了，主循环需要刷新 LCD。
 */
uint8_t app_ui_handle_key(app_key_event_t key);

/**
 * @brief 获取当前菜单状态，只读给显示模块使用。
 */
const app_ui_state_t *app_ui_get_state(void);

#endif
