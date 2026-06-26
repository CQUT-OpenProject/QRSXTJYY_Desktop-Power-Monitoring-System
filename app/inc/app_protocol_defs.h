/**
 * @file app_protocol_defs.h
 * @brief 二进制帧协议公共常量。
 *
 * 设备端固件和上位机工具（Rust serial_cmd）共享同一套定义。
 * 修改帧格式时只改这一个文件，两端保持一致。
 *
 * 帧结构（小端序）：
 *   SOF0 | SOF1 | VERSION | TYPE | SEQ | LEN_L | LEN_H
 *   | PAYLOAD[0..LEN-1] | CRC16_L | CRC16_H
 *
 * CRC16 覆盖 VERSION 到 PAYLOAD 末尾（不含 SOF 和 CRC 本身）。
 */
#ifndef APP_PROTOCOL_DEFS_H
#define APP_PROTOCOL_DEFS_H

/** 帧头第一字节。 */
#define APP_PROTOCOL_SOF0              0xA5U

/** 帧头第二字节。 */
#define APP_PROTOCOL_SOF1              0x5AU

/** 协议版本号。 */
#define APP_PROTOCOL_VERSION           0x01U

/** 帧类型：上位机→设备 命令。 */
#define APP_PROTOCOL_TYPE_COMMAND      0x01U

/** 帧类型：设备→上位机 命令响应。 */
#define APP_PROTOCOL_TYPE_RESPONSE     0x81U

/** 帧类型：设备→上位机 主动事件（自动上报等）。 */
#define APP_PROTOCOL_TYPE_EVENT        0x82U

/** 帧类型：设备→上位机 错误通知。 */
#define APP_PROTOCOL_TYPE_ERROR        0x83U

/** 帧类型：设备→上位机 屏幕截图数据（临时调试用，payload 为可打印文本/十六进制）。 */
#define APP_PROTOCOL_TYPE_SHOT         0x84U

/**
 * 单帧 payload 最大字节数。
 *
 * 上位机和设备端都必须遵守。超过此长度的帧一律丢弃并回报 ERR BAD_FRAME。
 */
#define APP_PROTOCOL_MAX_PAYLOAD       512U

#endif /* APP_PROTOCOL_DEFS_H */
