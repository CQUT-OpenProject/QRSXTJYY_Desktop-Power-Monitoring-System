/**
 * @file app.c
 * @brief 应用层总入口。
 *
 * 负责进入应用层初始化和主循环。上电启动顺序由 app_startup 模块集中编排，
 * 避免 main 和运行循环直接依赖具体外设初始化细节。
 */
#include "app.h"

#include "app_adc.h"
#include "app_capture.h"
#include "app_command.h"
#include "app_display.h"
#include "app_keys.h"
#include "app_monitor_state.h"
#include "app_protocol.h"
#include "app_startup.h"
#include "app_ui.h"

void app_init(void)
{
    app_startup_init();
}

/**
 * @brief 应用主循环。
 *
 * 调度策略：
 * 1. 中断只记录边沿、DMA 完成等实时事件；
 * 2. 计算、协议处理、LCD 刷新均在主循环完成；
 * 3. 同一轮循环共用 now_us，避免不同模块使用不一致的时间基准。
 */
void app_run(void)
{
    static app_monitor_state_t display_state;
    char report_line[APP_COMMAND_RESPONSE_LINE_MAX];
    uint32_t now_us;
    uint8_t display_changed;
    app_key_event_t key;

    while (1) {
        now_us = app_capture_get_time_us();

        display_changed = app_protocol_task(now_us);
        app_capture_task();
        app_adc_task();

        if (app_command_get_auto_report_enabled() != 0U &&
            app_capture_take_report() != 0U) {
            app_monitor_state_read(&display_state);
            app_command_format_status(&display_state, report_line, sizeof(report_line));
            app_protocol_send_report_line(report_line);
        }

        key = app_keys_poll(now_us);
        if (app_ui_handle_key(key) != 0U) {
            display_changed = 1U;
        }

        app_monitor_state_read(&display_state);
        if (display_changed != 0U) {
            app_display_request_refresh();
        }
        app_display_task(&display_state, app_ui_get_state());
    }
}
