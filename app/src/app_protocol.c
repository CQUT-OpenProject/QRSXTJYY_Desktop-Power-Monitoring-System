#include "app_protocol.h"

#include "app_command.h"
#include "app_config.h"
#include "app_protocol_defs.h"
#include "misc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#include <stdint.h>

#define APP_PROTOCOL_RX_RING_SIZE 256U
#define APP_PROTOCOL_FRAME_TIMEOUT_US 300000U
#define APP_PROTOCOL_COMMAND_PAYLOAD_MAX APP_PROTOCOL_MAX_PAYLOAD

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

/*
 * 数据所有权：
 * - USART1 ISR (USART1_IRQHandler) 只写 s_rx_ring 并推进 s_rx_head；
 * - 主循环 (app_protocol_task) 读 s_rx_ring，推进 s_rx_tail；
 * - s_rx_head / s_rx_tail / s_rx_overflow 均为 volatile，ISR 和主循环共享；
 * - 主循环读 tail 时关中断取 head，避免读到一个中间值。
 */
static volatile uint8_t s_rx_ring[APP_PROTOCOL_RX_RING_SIZE];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;
static volatile uint8_t s_rx_overflow;

/* 帧解析状态机变量 — 全部在主循环上下文中使用，ISR 不访问 */
static app_protocol_rx_state_t s_rx_state;
static uint32_t s_last_frame_byte_us;
static uint16_t s_rx_crc;
static uint8_t s_rx_version;
static uint8_t s_rx_type;
static uint8_t s_rx_seq;
static uint16_t s_rx_payload_len;
static uint16_t s_rx_payload_index;
static uint16_t s_rx_received_crc;
static uint8_t s_rx_payload_too_long;
static char s_rx_payload[APP_PROTOCOL_COMMAND_PAYLOAD_MAX + 1U];

/**
 * @brief CRC16/MODBUS：逐位累加一个字节到校验值。
 */
static uint16_t crc16_modbus_update(uint16_t crc, uint8_t byte)
{
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

/** 重置帧解析状态机和接收缓存。 */
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

/** 通过 USART1 阻塞发送一个字节。 */
static void usart_send_byte(uint8_t byte)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(USART1, (uint16_t)byte);
}

/** 计算文本 payload 字节长度（不含结尾 NUL）。 */
static uint16_t text_payload_length(const char *text)
{
    uint16_t len = 0U;

    if (text == 0) {
        return 0U;
    }

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

/** 发送一字节并同时累加 CRC。 */
static void send_crc_protected_byte(uint8_t byte, uint16_t *crc)
{
    *crc = crc16_modbus_update(*crc, byte);
    usart_send_byte(byte);
}

/**
 * @brief 发送一帧二进制协议帧，payload 为文本。
 *
 * 帧结构（小端序）：
 *   SOF0 | SOF1 | VERSION | TYPE | SEQ | LEN_L | LEN_H
 *   | PAYLOAD[0..LEN-1] | CRC16_L | CRC16_H
 */
static void send_text_frame(uint8_t type, uint8_t seq, const char *payload)
{
    uint16_t crc = 0xFFFFU;
    uint16_t len = text_payload_length(payload);
    uint16_t i;

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
    usart_send_byte((uint8_t)(crc >> 8U));
}

/** 发送 TYPE=0x83 坏帧错误响应。 */
static void send_protocol_error(uint8_t seq)
{
    send_text_frame((uint8_t)APP_PROTOCOL_TYPE_ERROR, seq, "ERR BAD_FRAME");
}

/** 检查接收 payload 是否全部为可打印 ASCII。 */
static uint8_t payload_is_printable_ascii(void)
{
    uint16_t i;

    for (i = 0U; i < s_rx_payload_len; i++) {
        uint8_t c = (uint8_t)s_rx_payload[i];
        if (c < 32U || c > 126U) {
            return 0U;
        }
    }
    return 1U;
}

/** 校验并分发一帧命令消息，返回 1=系统状态变化需刷 LCD。 */
static uint8_t dispatch_command_frame(void)
{
    app_command_result_t result;
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

/** 将一字节送入帧解析状态机；返回 1 表示状态变化。 */
static uint8_t parser_accept_byte(uint8_t byte)
{
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

/** USART ISR：将一字节压入环形缓冲。 */
static void rx_ring_push(uint8_t byte)
{
    uint8_t next = (uint8_t)(s_rx_head + 1U);

    if (next == s_rx_tail) {
        s_rx_overflow = 1U;
        return;
    }
    s_rx_ring[s_rx_head] = byte;
    s_rx_head = next;
}

/**
 * 主循环：从环形缓冲取出一字节。
 * 关中断取 head，保证读到一致的状态。
 */
static uint8_t rx_ring_pop(uint8_t *byte)
{
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

/** 读取并清除环形缓冲溢出标志。 */
static uint8_t rx_ring_take_overflow(void)
{
    uint8_t overflow;

    __disable_irq();
    overflow = s_rx_overflow;
    s_rx_overflow = 0U;
    __enable_irq();

    return overflow;
}

/**
 * @brief 初始化 USART1 协议收发通道。
 *
 * 引脚：PA9(TX) 复用推挽输出，PA10(RX) 浮空输入。
 * 配置后发送 OK COURSE1 READY 事件帧。
 */
void app_protocol_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = APP_UART_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);

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
 * @brief 串口协议周期任务：从环形缓冲取字节送入解析状态机。
 */
uint8_t app_protocol_task(uint32_t now_us)
{
    uint8_t byte;
    uint8_t monitor_changed = 0U;

    if (rx_ring_take_overflow() != 0U) {
        parser_reset();
    }

    /* 超时保护：半帧间隔超过 300 ms 则重置解析器 */
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
 * @brief USART1 RXNE 中断：将收到的字节压入环形缓冲。
 */
void app_protocol_usart1_irq_handler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        rx_ring_push((uint8_t)(USART_ReceiveData(USART1) & 0xFFU));
    }
}

/**
 * @brief 发送 TYPE=0x82 自动上报事件帧。
 */
void app_protocol_send_report_line(const char *line)
{
    send_text_frame((uint8_t)APP_PROTOCOL_TYPE_EVENT, 0U, line);
}

/**
 * @brief 发送 TYPE=0x84 屏幕截图帧（临时调试用）。
 */
void app_protocol_send_shot_line(const char *line)
{
    send_text_frame((uint8_t)APP_PROTOCOL_TYPE_SHOT, 0U, line);
}
