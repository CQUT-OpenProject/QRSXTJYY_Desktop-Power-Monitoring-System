/**
 * @file app_command.h
 * @brief 串口命令文本处理模块接口。
 *
 * app_command 负责解释帧 payload 中携带的命令文本并生成响应文本，
 * 不接触帧头、长度、序号、CRC 等二进制帧结构。
 * 输入是 0 结尾的命令文本，输出是若干行响应文本 payload。
 */
#ifndef APP_COMMAND_H
#define APP_COMMAND_H

#include "app_monitor_state.h"

#include <stdint.h>

#define APP_COMMAND_INPUT_MAX          64U
#define APP_COMMAND_RESPONSE_MAX        4U
#define APP_COMMAND_RESPONSE_LINE_MAX 256U

/**
 * @brief 串口命令处理结果。
 *
 * app_command 只处理帧 payload 中的命令文本，不负责帧头、长度、序号和 CRC。
 * 响应文本由 app_protocol 封装为二进制响应帧发送。
 */
typedef struct {
    /** 非零表示命令改变了系统状态，需要刷新 LCD。 */
    uint8_t monitor_changed;

    /** responses 中有效响应行数。 */
    uint8_t response_count;

    /** 响应文本 payload，不含帧头、CRC 和换行符。 */
    char responses[APP_COMMAND_RESPONSE_MAX][APP_COMMAND_RESPONSE_LINE_MAX];
} app_command_result_t;

/**
 * @brief 初始化命令模块内部状态（自动上报开关等）。
 *
 * 在 app_startup 初始化序列中调用，早于主循环。
 */
void app_command_init(void);

/**
 * @brief 处理一行命令文本并生成响应。
 *
 * @param line   从命令帧 payload 提取的完整命令文本，空值返回 BAD_COMMAND。
 * @param result 输出参数，写入响应文本 payload 和状态变化标志。
 *
 * 函数内部先复制输入文本到局部缓冲再做裁剪和匹配，不修改 ISR 缓冲区。
 */
void app_command_handle_line(const char *line, app_command_result_t *result);

/**
 * @brief 设置自动上报开关。
 *
 * LCD 菜单和串口命令都通过此接口修改，保证两边读到同一份状态。
 */
void app_command_set_auto_report_enabled(uint8_t enabled);

/**
 * @brief 读取自动上报开关状态。
 */
uint8_t app_command_get_auto_report_enabled(void);

/**
 * @brief 将监控快照格式化为一行 STATUS 响应文本 payload。
 *
 * STATUS? 命令和 TYPE=0x82 自动上报事件帧共用此接口，
 * 修改 STATUS 输出格式时只改一处。
 */
void app_command_format_status(const app_monitor_state_t *state,
                               char *line,
                               uint16_t line_size);

#endif
