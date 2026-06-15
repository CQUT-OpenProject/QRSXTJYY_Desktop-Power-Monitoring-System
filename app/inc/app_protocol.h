/**
 * @file app_protocol.h
 * @brief 通信协议应用模块接口。
 */
#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

/**
 * @brief 初始化通信协议模块。
 */
void app_protocol_init(void);

/**
 * @brief 串口协议周期任务。
 *
 * 这里只处理串口收发：把中断里收到的字节拼成一行，再交给 app_command。
 * PWM、DAC 和 LCD 的事不放在这个文件里做。
 *
 * @return 1 表示命令执行改变了监控状态或显示状态，0 表示无变化。
 */
uint8_t app_protocol_task(uint32_t now_us);

/**
 * @brief USART1 中断入口转发函数。
 */
void app_protocol_usart1_irq_handler(void);

/**
 * @brief 发送一行 ASCII 文本，自动追加 CRLF。
 */
void app_protocol_send_line(const char *line);

#endif
