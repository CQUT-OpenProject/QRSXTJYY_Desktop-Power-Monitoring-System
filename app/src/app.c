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

/**
 * @brief 初始化应用层所有启动项。
 */
void app_init(void)
{
    app_startup_init();
}

/**
 * @brief 执行应用主循环，调度串口、捕获、按键、UI 和显示刷新。
 */
void app_run(void)
{
    // display_state 里带有 128 点 DAC 波形表，放成 static 可以少占栈空间。
    static app_monitor_state_t display_state;
    // 自动上报要发送的状态文本缓冲区
    char report_line[APP_COMMAND_RESPONSE_LINE_MAX];
    // 捕获模块取出的待上报频率，单位 Hz * 100
    uint32_t report_freq_x100;
    // 本轮主循环共用的微秒时间戳
    uint32_t now_us;
    // 本轮是否需要请求 LCD 刷新
    uint8_t display_changed;
    // 按键模块输出的消抖后事件
    app_key_event_t key;

    while (1) {
        // 这一轮循环共用同一个 now_us。串口帧超时和按键消抖都按它算。
        now_us = app_capture_get_time_us();

        // 串口协议层检查有没有完整命令。命令改了显示内容时，返回值会置 1。
        display_changed = app_protocol_task(now_us);

        // 信号超时、上报节流这些不急的事放主循环里做。
        // TIM2 中断只快速记下捕获时间。
        app_capture_task();

        // ADC 模块：检查 ISR 标记的新数据，触发电参数计算。
        app_adc_task();

        if (app_command_get_auto_report_enabled() != 0U &&
            app_capture_take_report(&report_freq_x100) != 0U) {
            // take_report 只表示该发一条自动上报了。上报内容重新读快照生成，
            // 和 STATUS? 用同一种格式。
            (void)report_freq_x100;

            app_monitor_state_read(&display_state);
            app_command_format_status(&display_state, report_line, sizeof(report_line));
            app_protocol_send_report_plain(report_line);
        }

        // 按键扫描返回的是消抖后的“事件”，不是 GPIO 当前电平。
        // UI 状态机处理事件后，如果页面内容变化，就请求显示刷新。
        key = app_keys_poll(now_us);
        if (app_ui_handle_key(key) != 0U) {
            display_changed = 1U;
        }

        // 每轮都更新快照。LCD 是否真的重画，由 app_display_task 决定。
        app_monitor_state_read(&display_state);
        if (display_changed != 0U) {
            app_display_request_refresh();
        }
        app_display_task(&display_state, app_ui_get_state());
    }
}
