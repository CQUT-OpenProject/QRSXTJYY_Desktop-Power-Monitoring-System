/**
 * @file app_command.h
 * @brief 串口命令面接口。
 */
#ifndef APP_COMMAND_H
#define APP_COMMAND_H

#include "app_monitor_state.h"

#include <stdint.h>

#define APP_COMMAND_RESPONSE_MAX 4U
#define APP_COMMAND_RESPONSE_LINE_MAX 160U

/**
 * @brief 一行串口命令处理后的结果。
 *
 * 命令处理到这里就停在“文本行”这一层：输入是一行命令，输出也是几行
 * 准备发送的文本。至于怎么把这些文本发到 USART1，由 app_protocol 处理。
 */
typedef struct {
    /**
     * @brief 这条命令是否需要刷新 LCD 上的监控内容。
     *
     * 修改 PWM、DAC 或上报开关时置 1；只查询状态时保持 0。
     */
    uint8_t monitor_changed;

    /**
     * @brief responses 数组里实际有效的行数。
     */
    uint8_t response_count;

    /**
     * @brief 待发送的 ASCII 响应行，不带结尾的 CRLF。
     *
     * 每行末尾的 CRLF 交给 app_protocol_send_line 添加，命令处理函数只管
     * 准备正文。
     */
    char responses[APP_COMMAND_RESPONSE_MAX][APP_COMMAND_RESPONSE_LINE_MAX];
} app_command_result_t;

/**
 * @brief 初始化串口命令面保存的状态。
 */
void app_command_init(void);

/**
 * @brief 处理一行完整命令。
 *
 * @param line 从串口收到的一行 ASCII 文本，函数会先复制到临时缓冲区。
 * @param result 保存响应文本和是否需要刷新 LCD。
 */
void app_command_handle_line(const char *line, app_command_result_t *result);

/**
 * @brief 设置测量结果是否自动通过串口上报。
 */
void app_command_set_auto_report_enabled(uint8_t enabled);

/**
 * @brief 读取测量结果自动上报开关。
 */
uint8_t app_command_get_auto_report_enabled(void);

/**
 * @brief 把当前监控快照格式化成一行 STATUS 响应。
 *
 * STATUS? 命令和自动上报都走这里，后面改串口状态格式时只改一处。
 */
void app_command_format_status(const app_monitor_state_t *state,
                               char *line,
                               uint16_t line_size);

#endif
