/**
 * @file app_command.h
 * @brief 串口命令面接口。
 */
#ifndef APP_COMMAND_H
#define APP_COMMAND_H

#include "app_monitor_state.h"

#include <stdint.h>

#define APP_COMMAND_INPUT_MAX 64U
#define APP_COMMAND_RESPONSE_MAX 4U
#define APP_COMMAND_RESPONSE_LINE_MAX 256U

/**
 * @brief 一条串口命令文本处理后的结果。
 *
 * 命令处理到这里就停在“ASCII 文本”这一层：输入是一条命令，输出也是几条
 * 准备发送的响应文本。至于怎么封装成串口帧，由 app_protocol 处理。
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
     * @brief 待发送的 ASCII 响应 payload，不带结尾的 CRLF。
     *
     * 命令处理函数只准备正文，串口帧头、长度和 CRC 交给 app_protocol 添加。
     */
    char responses[APP_COMMAND_RESPONSE_MAX][APP_COMMAND_RESPONSE_LINE_MAX];
} app_command_result_t;

/**
 * @brief 初始化串口命令面保存的状态。
 */
void app_command_init(void);

/**
 * @brief 处理一条完整命令文本。
 *
 * @param line 从串口帧 payload 收到的 ASCII 文本，函数会先复制到临时缓冲区。
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
