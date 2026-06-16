#include "app_protocol.h"

#include "app_command.h"
#include "misc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#include <stdint.h>

#define APP_PROTOCOL_BAUDRATE 115200U
#define APP_PROTOCOL_SOF0 0xA5U
#define APP_PROTOCOL_SOF1 0x5AU
#define APP_PROTOCOL_VERSION 0x01U

#define APP_PROTOCOL_TYPE_COMMAND 0x01U
#define APP_PROTOCOL_TYPE_RESPONSE 0x81U
#define APP_PROTOCOL_TYPE_EVENT 0x82U
#define APP_PROTOCOL_TYPE_ERROR 0x83U

#define APP_PROTOCOL_RX_RING_SIZE 256U
#define APP_PROTOCOL_FRAME_TIMEOUT_US 300000U
#define APP_PROTOCOL_COMMAND_PAYLOAD_MAX (APP_COMMAND_INPUT_MAX - 1U)

typedef enum {
    APP_PROTOCOL_RX_SOF0 = 0,
    APP_PROTOCOL_RX_SOF1,
    APP_PROTOCOL_RX_VER,
    APP_PROTOCOL_RX_TYPE,
    APP_PROTOCOL_RX_SEQ,
    APP_PROTOCOL_RX_LEN_L,
    APP_PROTOCOL_RX_LEN_H,
    APP_PROTOCOL_RX_PAYLOAD,
    APP_PROTOCOL_RX_CRC_L,
    APP_PROTOCOL_RX_CRC_H
} app_protocol_rx_state_t;

// USART1 中断写入、主循环读取的接收环形缓冲区
static volatile uint8_t s_rx_ring[APP_PROTOCOL_RX_RING_SIZE];
// 环形缓冲写指针，由中断在收到新字节后推进
static volatile uint8_t s_rx_head;
// 环形缓冲读指针，由主循环解析字节时推进
static volatile uint8_t s_rx_tail;
// 环形缓冲溢出标志，主循环读取后会清零并重置解析器
static volatile uint8_t s_rx_overflow;

// 当前协议帧解析状态
static app_protocol_rx_state_t s_rx_state;
// 最近一次收到帧内字节的时间，用于判断半帧超时
static uint32_t s_last_frame_byte_us;
// 接收帧实时计算出的 CRC16/MODBUS 校验值
static uint16_t s_rx_crc;
// 接收帧里的协议版本字段
static uint8_t s_rx_version;
// 接收帧里的消息类型字段
static uint8_t s_rx_type;
// 接收帧里的序号字段，响应和错误帧会原样带回
static uint8_t s_rx_seq;
// 接收帧声明的 payload 字节长度
static uint16_t s_rx_payload_len;
// 当前已经接收的 payload 字节数
static uint16_t s_rx_payload_index;
// 接收帧尾部携带的 CRC16 校验值
static uint16_t s_rx_received_crc;
// payload 长度超过命令缓冲容量时置位，仍读完整帧但不执行命令
static uint8_t s_rx_payload_too_long;
// 命令文本 payload 缓冲区，额外保留 1 字节给字符串结束符
static char s_rx_payload[APP_PROTOCOL_COMMAND_PAYLOAD_MAX + 1U];

/**
 * @brief 把一个字节累加进 CRC16/MODBUS 校验值。
 */
static uint16_t crc16_modbus_update(uint16_t crc, uint8_t byte)
{
    // 当前处理的 CRC 位序号
    uint8_t i;

    crc ^= byte;
    for (i = 0U; i < 8U; i++) {
        if ((crc & 0x0001U) != 0U) {
            crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
        } else {
            crc = (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

/**
 * @brief 重置协议帧解析状态机和当前帧缓存。
 */
static void parser_reset(void)
{
    s_rx_state = APP_PROTOCOL_RX_SOF0;
    s_rx_crc = 0xFFFFU;
    s_rx_version = 0U;
    s_rx_type = 0U;
    s_rx_seq = 0U;
    s_rx_payload_len = 0U;
    s_rx_payload_index = 0U;
    s_rx_received_crc = 0U;
    s_rx_payload_too_long = 0U;
    s_rx_payload[0] = '\0';
}

/**
 * @brief 通过 USART1 阻塞发送单个字节。
 */
static void usart_send_byte(uint8_t byte)
{
    // 等发送寄存器空了，再写入一个字节
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(USART1, (uint16_t)byte);
}

/**
 * @brief 计算文本 payload 的字节长度。
 */
static uint16_t text_payload_length(const char *text)
{
    // 待发送文本 payload 的长度，不包含字符串结束符
    uint16_t len = 0U;

    if (text == 0) {
        return 0U;
    }

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

/**
 * @brief 发送一个参与 CRC 计算的帧字段字节。
 */
static void send_crc_protected_byte(uint8_t byte, uint16_t *crc)
{
    *crc = crc16_modbus_update(*crc, byte);
    usart_send_byte(byte);
}

/**
 * @brief 按应用协议格式发送一帧文本消息。
 */
static void send_text_frame(uint8_t type, uint8_t seq, const char *payload)
{
    // 发送帧实时累计的 CRC16/MODBUS 校验值
    uint16_t crc = 0xFFFFU;
    // 待发送文本 payload 长度
    uint16_t len = text_payload_length(payload);
    // payload 字节发送下标
    uint16_t i;

    // 发送帧头、版本、类型、序号、长度和 payload，最后发送 CRC 校验值
    usart_send_byte((uint8_t)APP_PROTOCOL_SOF0);
    usart_send_byte((uint8_t)APP_PROTOCOL_SOF1);
    send_crc_protected_byte((uint8_t)APP_PROTOCOL_VERSION, &crc);
    send_crc_protected_byte(type, &crc);
    send_crc_protected_byte(seq, &crc);
    send_crc_protected_byte((uint8_t)(len & 0xFFU), &crc);
    send_crc_protected_byte((uint8_t)(len >> 8U), &crc);
    for (i = 0U; i < len; i++) {
        send_crc_protected_byte((uint8_t)payload[i], &crc);
    }
    usart_send_byte((uint8_t)(crc & 0xFFU));
    usart_send_byte((uint8_t)(crc >> 8U)); // 小端序发送 CRC
}

/**
 * @brief 向主机发送坏帧错误响应。
 */
static void send_protocol_error(uint8_t seq)
{
    send_text_frame((uint8_t)APP_PROTOCOL_TYPE_ERROR, seq, "ERR BAD_FRAME");
}

/**
 * @brief 检查接收 payload 是否全部为可打印 ASCII 字符。
 */
static uint8_t payload_is_printable_ascii(void)
{
    // payload 扫描下标
    uint16_t i;

    for (i = 0U; i < s_rx_payload_len; i++) {
        uint8_t c = (uint8_t)s_rx_payload[i];
        if (c < 32U || c > 126U) {
            return 0U;
        }
    }
    return 1U;
}

/**
 * @brief 校验并分发一帧命令消息。
 */
static uint8_t dispatch_command_frame(void)
{
    // 命令模块返回的响应文本和状态变化结果
    app_command_result_t result;
    // 响应行发送下标
    uint8_t i;

    if (s_rx_version != APP_PROTOCOL_VERSION ||
        s_rx_type != APP_PROTOCOL_TYPE_COMMAND ||
        s_rx_payload_too_long != 0U ||
        payload_is_printable_ascii() == 0U) {
        send_protocol_error(s_rx_seq);
        return 0U;
    }

    s_rx_payload[s_rx_payload_len] = '\0';
    app_command_handle_line(s_rx_payload, &result);
    for (i = 0U; i < result.response_count; i++) {
        send_text_frame((uint8_t)APP_PROTOCOL_TYPE_RESPONSE,
                        s_rx_seq,
                        result.responses[i]);
    }
    return result.monitor_changed;
}

/**
 * @brief 将一个接收字节送入协议解析状态机。
 */
static uint8_t parser_accept_byte(uint8_t byte)
{
    // 本字节完成解析后，监控配置是否发生变化
    uint8_t monitor_changed = 0U;

    switch (s_rx_state) {
    case APP_PROTOCOL_RX_SOF0:
        if (byte == APP_PROTOCOL_SOF0) {
            s_rx_state = APP_PROTOCOL_RX_SOF1;
        }
        break;

    case APP_PROTOCOL_RX_SOF1:
        if (byte == APP_PROTOCOL_SOF1) {
            s_rx_crc = 0xFFFFU;
            s_rx_payload_index = 0U;
            s_rx_payload_too_long = 0U;
            s_rx_state = APP_PROTOCOL_RX_VER;
        } else if (byte != APP_PROTOCOL_SOF0) {
            s_rx_state = APP_PROTOCOL_RX_SOF0;
        }
        break;

    case APP_PROTOCOL_RX_VER:
        s_rx_version = byte;
        s_rx_crc = crc16_modbus_update(s_rx_crc, byte);
        s_rx_state = APP_PROTOCOL_RX_TYPE;
        break;

    case APP_PROTOCOL_RX_TYPE:
        s_rx_type = byte;
        s_rx_crc = crc16_modbus_update(s_rx_crc, byte);
        s_rx_state = APP_PROTOCOL_RX_SEQ;
        break;

    case APP_PROTOCOL_RX_SEQ:
        s_rx_seq = byte;
        s_rx_crc = crc16_modbus_update(s_rx_crc, byte);
        s_rx_state = APP_PROTOCOL_RX_LEN_L;
        break;

    case APP_PROTOCOL_RX_LEN_L:
        s_rx_payload_len = byte;
        s_rx_crc = crc16_modbus_update(s_rx_crc, byte);
        s_rx_state = APP_PROTOCOL_RX_LEN_H;
        break;

    case APP_PROTOCOL_RX_LEN_H:
        s_rx_payload_len |= (uint16_t)((uint16_t)byte << 8U);
        s_rx_crc = crc16_modbus_update(s_rx_crc, byte);
        s_rx_payload_index = 0U;
        s_rx_payload_too_long =
            s_rx_payload_len > APP_PROTOCOL_COMMAND_PAYLOAD_MAX ? 1U : 0U;
        if (s_rx_payload_len == 0U) {
            s_rx_payload[0] = '\0';
            s_rx_state = APP_PROTOCOL_RX_CRC_L;
        } else {
            s_rx_state = APP_PROTOCOL_RX_PAYLOAD;
        }
        break;

    case APP_PROTOCOL_RX_PAYLOAD:
        s_rx_crc = crc16_modbus_update(s_rx_crc, byte);
        if (s_rx_payload_index < APP_PROTOCOL_COMMAND_PAYLOAD_MAX) {
            s_rx_payload[s_rx_payload_index] = (char)byte;
        }
        s_rx_payload_index++;
        if (s_rx_payload_index >= s_rx_payload_len) {
            if (s_rx_payload_len <= APP_PROTOCOL_COMMAND_PAYLOAD_MAX) {
                s_rx_payload[s_rx_payload_len] = '\0';
            } else {
                s_rx_payload[APP_PROTOCOL_COMMAND_PAYLOAD_MAX] = '\0';
            }
            s_rx_state = APP_PROTOCOL_RX_CRC_L;
        }
        break;

    case APP_PROTOCOL_RX_CRC_L:
        s_rx_received_crc = byte;
        s_rx_state = APP_PROTOCOL_RX_CRC_H;
        break;

    case APP_PROTOCOL_RX_CRC_H:
        s_rx_received_crc |= (uint16_t)((uint16_t)byte << 8U);
        if (s_rx_received_crc == s_rx_crc) {
            monitor_changed = dispatch_command_frame();
        }
        parser_reset();
        break;

    default:
        parser_reset();
        break;
    }

    return monitor_changed;
}

/**
 * @brief 在 USART 中断中把收到的字节写入环形缓冲。
 */
static void rx_ring_push(uint8_t byte)
{
    // 写入当前字节后预计推进到的环形缓冲位置
    uint8_t next = (uint8_t)(s_rx_head + 1U);

    if (next == s_rx_tail) {
        s_rx_overflow = 1U;
        return;
    }
    s_rx_ring[s_rx_head] = byte;
    s_rx_head = next;
}

/**
 * @brief 从接收环形缓冲中取出一个字节。
 */
static uint8_t rx_ring_pop(uint8_t *byte)
{
    // 是否成功从环形缓冲取出一个字节
    uint8_t has_byte = 0U;

    __disable_irq();
    if (s_rx_tail != s_rx_head) {
        *byte = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint8_t)(s_rx_tail + 1U);
        has_byte = 1U;
    }
    __enable_irq();

    return has_byte;
}

/**
 * @brief 读取并清除接收环形缓冲溢出标志。
 */
static uint8_t rx_ring_take_overflow(void)
{
    // 本次读取到的溢出标志快照
    uint8_t overflow;

    __disable_irq();
    overflow = s_rx_overflow;
    s_rx_overflow = 0U;
    __enable_irq();

    return overflow;
}

/**
 * @brief 初始化 USART1 应用协议收发通道。
 */
void app_protocol_init(void)
{
    // USART1 TX/RX 引脚配置
    GPIO_InitTypeDef gpio;
    // USART1 通信参数配置
    USART_InitTypeDef usart;
    // USART1 接收中断优先级配置
    NVIC_InitTypeDef nvic;

    // USART1 使用 PA9(TX) 和 PA10(RX)，打开端口时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    // 配置 PA9 复用推挽输出，用来把串口数据发给板载 CH340
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    // PA10 是串口接收脚，使用浮空输入
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = APP_PROTOCOL_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);

    // RXNE 中断表示收到一个字节，ISR 会把字节放进环形缓冲
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_rx_overflow = 0U;
    s_last_frame_byte_us = 0U;
    parser_reset();

    USART_Cmd(USART1, ENABLE);
    send_text_frame((uint8_t)APP_PROTOCOL_TYPE_EVENT, 0U, "OK COURSE1 READY");
}

/**
 * @brief 处理接收缓冲中的协议字节并执行完整命令帧。
 */
uint8_t app_protocol_task(uint32_t now_us)
{
    // 从环形缓冲中取出的待解析字节
    uint8_t byte;
    // 本轮任务是否导致监控配置变化
    uint8_t monitor_changed = 0U;

    if (rx_ring_take_overflow() != 0U) {
        parser_reset();
    }

    if (s_rx_state != APP_PROTOCOL_RX_SOF0 &&
        (now_us - s_last_frame_byte_us) >= APP_PROTOCOL_FRAME_TIMEOUT_US) {
        parser_reset();
    }

    while (rx_ring_pop(&byte) != 0U) {
        s_last_frame_byte_us = now_us;
        if (parser_accept_byte(byte) != 0U) {
            monitor_changed = 1U;
        }
    }

    return monitor_changed;
}

/**
 * @brief USART1 接收中断处理入口，转存收到的字节。
 */
void app_protocol_usart1_irq_handler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        rx_ring_push((uint8_t)(USART_ReceiveData(USART1) & 0xFFU));
    }
}

/**
 * @brief 以事件帧形式发送一行状态上报文本。
 */
void app_protocol_send_report_line(const char *line)
{
    send_text_frame((uint8_t)APP_PROTOCOL_TYPE_EVENT, 0U, line);
}
