/**
 * @file app_protocol.h
 * @brief 二进制协议帧收发模块接口。
 *
 * 负责 USART1 的字节收发、二进制帧组帧与解析。
 * 收到完整命令帧后，将文本 payload 交给 app_command 处理；
 * 响应文本由本模块封装为 TYPE=0x81 二进制响应帧发送。
 */
#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

/**
 * @brief 初始化 USART1 协议收发通道。
 *
 * 配置 PA9(TX)/PA10(RX) GPIO、USART1 波特率和 RXNE 中断，
 * 在 USART 使能后发送 OK COURSE1 READY 事件帧。
 */
void app_protocol_init(void);

/**
 * @brief 串口协议周期任务，在主循环中调用。
 *
 * 从环形缓冲逐个取出接收字节送入解析状态机。
 * 完整命令帧组装后，其文本 payload 交由 app_command_handle_line 处理。
 *
 * @param now_us 当前微秒时间戳，用于帧间超时判断。
 * @return 1 表示命令改变了系统状态，下一轮主循环应刷新 LCD。
 */
uint8_t app_protocol_task(uint32_t now_us);

/**
 * @brief USART1 RXNE 中断入口转发函数。
 *
 * ISR 上下文：只将收到的字节压入环形缓冲，不做帧解析。
 * 由 stm32f10x_it.c 中的 USART1_IRQHandler 调用。
 */
void app_protocol_usart1_irq_handler(void);

/**
 * @brief 发送 TYPE=0x82 自动上报或事件帧。
 *
 * @param line 文本 payload，不含 CRLF。函数会封装为二进制协议帧。
 */
void app_protocol_send_report_line(const char *line);

#endif
